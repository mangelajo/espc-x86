/*
 * Based on FabGL - ESP32 Graphics Library
 * Original project by Fabrizio Di Vittorio
 * https://github.com/fdivitto/FabGL
 *
 * Original Copyright (c) 2019-2022 Fabrizio Di Vittorio
 *
 * Modifications and further development:
 * Copyright (c) 2026 Jesus Martinez-Mateo
 * Author: Jesus Martinez-Mateo <jesus.martinez.mateo@gmail.com>
 *
 * This file is part of a derived work from FabGL
 * and is distributed under the terms of the
 * GNU General Public License version 3 or later.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "computer.h"
#include "setup.h"
#include "drivers/ipc.h"

#include "host/snapshot.h"

// Video cards
#include "video/cga.h"
#include "video/ega.h"
#include "video/hercules.h"
#include "video/tandy.h"

#include "esp_heap_caps.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#pragma GCC optimize ("O3")

// I/O expander (based on MCP23S17) ports

#define EXTIO_CONFIG                    0x00e0   // configuration port (see EXTIO_CONFIG_.... flags)
// whole 8 bit ports handling
#define EXTIO_DIRA                      0x00e1   // port A direction (0 = input, 1 = output)
#define EXTIO_DIRB                      0x00e2   // port B direction (0 = input, 1 = output)
#define EXTIO_PULLUPA                   0x00e3   // port A pullup enable (0 = disabled, 1 = enabled)
#define EXTIO_PULLUPB                   0x00e4   // port B pullup enable (0 = disabled, 1 = enabled)
#define EXTIO_PORTA                     0x00e5   // port A read/write
#define EXTIO_PORTB                     0x00e6   // port B read/write
// single GPIO handling
#define EXTIO_GPIOSEL                   0x00e7   // GPIO selection (0..7 = PA0..PA7, 8..15 = PB0..PB8)
#define EXTIO_GPIOCONF                  0x00e8   // selected GPIO direction and pullup (0 = input, 1 = output, 2 = input with pullup)
#define EXTIO_GPIO                      0x00e9   // selected GPIO read or write (0 = low, 1 = high)

// I/O expander configuration bits
#define EXTIO_CONFIG_AVAILABLE            0x01   // 1 = external IO available, 0 = not available
#define EXTIO_CONFIG_INT_POLARITY         0x02   // 1 = positive polarity, 0 = negative polarity (default)

using fabgl::i8086;

uint8_t *Computer::s_memory;

Computer::Computer() :
  #ifdef FABGL_EMULATED
  m_stepCallback(nullptr),
  #endif
  m_reset(false),
  m_paused(false),
  m_diskFilename(),
  m_diskChanged(),
  m_disk(),
  m_bootDrive(0),
  m_sysReqCallback(nullptr),
  m_baseDir(nullptr),
  m_adlib(nullptr),
  m_adlibGen(nullptr),
  m_gamepad(nullptr),
  m_btDriver(nullptr),
  m_dipPB(0x00)
{
}

Computer::~Computer()
{
  for (int i = 0; i < DISKCOUNT; i++) {
    free(m_diskFilename[i]);
    if (m_disk[i])
      fclose(m_disk[i]);
  }
  vTaskDelete(m_taskHandle);

  if (m_adlib) {
    m_adlibGen->stopProducerTask();
    delete m_adlibGen;
    delete m_adlib;
    m_adlibGen = nullptr;
    m_adlib = nullptr;
  }
}

bool Computer::FDC_IRQ6_Thunk(void *context)
{
  auto m = (Computer *) context;
  return m->m_PIC_master.signalInterrupt(6); // IRQ6 (FDC)
}

void Computer::init()
{
  Setup cfg;

  srand((uint32_t) time(NULL));

#ifdef BOARD_HAS_PSRAM
  printf("computer: Core %d using memory RAM (%d KB) in PSRAM\n", xPortGetCoreID(), RAM_SIZE / 1024);
  s_memory = (uint8_t *) heap_caps_malloc(RAM_SIZE, MALLOC_CAP_SPIRAM);
#else
  printf("computer: Core %d accessing directly to PSRAM\n", xPortGetCoreID());
  // To avoid PSRAM bug without -mfix-esp32-psram-cache-issue
  // core 0 can only work reliably with the lower 2 MB and core 1 only with the higher 2 MB.
  s_memory = (uint8_t *) (SOC_EXTRAM_DATA_LOW + (xPortGetCoreID() == 1 ? 2 * RAM_SIZE : 0));
#endif

  // Clear memory
  memset(s_memory, 0, RAM_SIZE);

  setupLoad(&cfg);

  m_video.init(s_memory);
  // Video cards
  switch ((video::VideoAdapterType) cfg.video) {

    case video::VideoAdapterType::EGA:
    {
      auto ega = new video::EGA();
      m_video.active(video::VideoAdapterType::EGA, ega, ega);
      break;
    }

    case video::VideoAdapterType::HGC:
    {
      auto hgc = new video::HGC();
      m_video.active(video::VideoAdapterType::HGC, hgc, hgc);
      break;
    }

    case video::VideoAdapterType::TGA:
    {
      auto tga = new video::Tandy();
      m_video.active(video::VideoAdapterType::TGA, tga, tga);
      break;
    }

    case video::VideoAdapterType::CGA:
    default:
    {
      auto cga = new video::CGA();
      m_video.active(video::VideoAdapterType::CGA, cga, cga);
      break;
    }
  }

  m_soundGen.attach(&m_waveGen);

  // AdLib sound card
  if (cfg.sound == 1) {

    m_adlib = new AdLib();
    m_adlibGen = new AdLibWaveformGenerator();

    // Initialize AdLib device and attach its generator
    AdLib::AudioConfig adlibCfg;
    adlibCfg.sampleRate = 16384;
    adlibCfg.masterVolume = 0.25f;
    m_adlib->init(adlibCfg);

    m_adlibGen->bind(m_adlib);
    m_adlibGen->setVolume(100);
    m_adlibGen->enable(true);
    m_soundGen.attach(m_adlibGen);
    m_adlibGen->startProducerTask(1, 5, 256);
  }

  // Start audio output (internal DAC / jack on TTGO VGA32)
  m_soundGen.play(true);
  m_soundGen.setVolume(68); // 50%

  m_DMA.init();

  m_PIT.init(this, PIT_IRQ0);

  // Keyboard
#if LEGACY_IBMPC_XT_8088
  m_i8255.init();
  m_i8255.setCallbacks(this, keyboardInterrupt, hostReq);
  // Configure XT DIP switches (example values; tune for your BIOS)
  m_i8255.setSW2(/*SW2[1..4]=*/0b1010, /*SW2[5]=*/true);
#else
  m_i8042.init();
  m_i8042.setCallbacks(this, keyboardInterrupt, mouseInterrupt, triggerReset, hostReq);
#endif

  // Joystick
  if (cfg.joystick == 1) {
    m_gamepad = new BTGamepad(&m_gameport);
    //m_btDriver = new BTGamepadDriver(m_gamepad);
    //m_btDriver->begin();
  }

  m_MC146818.init("PCEmulator");
  m_MC146818.setCallbacks(this, MC146818Interrupt);

  m_MCP23S17.begin();
  m_MCP23S17Sel = 0;

  // Floppy disk
  m_FDC.init();
  m_FDC.setCallbacks(this, FDC_IRQ6_Thunk);

  static char path[MAX_FILEPATH_LEN];

#ifdef NATIVE_BUILD
  // cfg.media_path (default: SD_MOUNT_PATH "/espc-x86/media") already
  // contains the prefix, so use it directly to avoid double-prefixing.
  strncpy(path, cfg.media_path, MAX_FILEPATH_LEN - 1);
  path[MAX_FILEPATH_LEN - 1] = 0;
#else
  sprintf(path, "%s%s", SD_MOUNT_PATH, cfg.media_path);
#endif

  setBaseDirectory(path);
  // Mount drives
  //setDriveImage(0, "tmpfs0.img");
  for (int i = 0; i < 4; i++) {
    if (cfg.drive[i][0] != 0) {
      setDriveImage(i, cfg.drive[i], 0, 0, 0);
    }
  }
  setBootDrive(cfg.boot);

  m_BIOS.init(this);

  i8086::setCallbacks(this, readPort, writePort, writeVideoMemory8, writeVideoMemory16, readVideoMemory8, readVideoMemory16, interrupt);
  i8086::setMemory(s_memory);

  m_reset = true;
}

#if 0

// Read BPB geometry (SPT/HEADS) from the partition boot sector if the disk has an MBR.
// Returns true if a plausible BPB was found.
static bool readBPBGeometryFromImage(FILE *fd, uint16_t &outSPT, uint16_t &outHeads) {

  uint8_t s0[512];
  fseek(fd, 0, SEEK_SET);
  if (fread(s0, 1, 512, fd) != 512)
    return false;

  // Must have 0x55AA signature to be a valid boot sector / MBR
  if (s0[510] != 0x55 || s0[511] != 0xAA)
    return false;

  auto looksLikeFATBootSector = [&](const uint8_t *bs) -> bool {
    // FAT VBR usually starts with a jump (EB xx 90 or E9 xx xx)
    const bool hasJump = (bs[0] == 0xEB && bs[2] == 0x90) || (bs[0] == 0xE9);
    const uint16_t bps = (uint16_t)bs[0x0B] | ((uint16_t)bs[0x0C] << 8);
    const uint8_t spc  = bs[0x0D];
    const uint16_t rsv = (uint16_t)bs[0x0E] | ((uint16_t)bs[0x0F] << 8);
    return hasJump && (bps == 512) && (spc != 0) && (rsv != 0);
  };

  // If sector 0 already looks like a FAT boot sector (superfloppy), read BPB directly.
  if (looksLikeFATBootSector(s0)) {
    outSPT   = (uint16_t)s0[0x18] | ((uint16_t)s0[0x19] << 8);
    outHeads = (uint16_t)s0[0x1A] | ((uint16_t)s0[0x1B] << 8);
    return (outSPT != 0 && outHeads != 0);
  }

  // Otherwise treat sector 0 as MBR and parse the first partition entry at 0x1BE.
  const uint8_t *pt = s0 + 0x1BE;

  // Find the first valid partition entry (type != 0 and LBA start != 0)
  for (int i = 0; i < 4; ++i) {
    const uint8_t *e = pt + i * 16;
    const uint8_t ptype = e[4];
    const uint32_t lbaStart =
      (uint32_t)e[8]  |
      ((uint32_t)e[9]  << 8) |
      ((uint32_t)e[10] << 16) |
      ((uint32_t)e[11] << 24);

    if (ptype != 0 && lbaStart != 0) {
      // Read the partition boot sector (VBR/PBR)
      uint8_t vbr[512];
      fseek(fd, (long)(lbaStart * 512ULL), SEEK_SET);
      if (fread(vbr, 1, 512, fd) != 512)
        return false;

      if (vbr[510] != 0x55 || vbr[511] != 0xAA)
        return false;

      if (!looksLikeFATBootSector(vbr))
        return false;

      outSPT   = (uint16_t)vbr[0x18] | ((uint16_t)vbr[0x19] << 8);
      outHeads = (uint16_t)vbr[0x1A] | ((uint16_t)vbr[0x1B] << 8);
      return (outSPT != 0 && outHeads != 0);
    }
  }

  return false;
}
#endif

void Computer::setDriveImage(int drive, char const *filename, int cylinders, int heads, int sectors)
{
  if (m_disk[drive]) {
    fclose(m_disk[drive]);
    m_disk[drive] = nullptr;
  }

  if (m_diskFilename[drive]) {
    free(m_diskFilename[drive]);
    m_diskFilename[drive] = nullptr;
  }

  m_BIOS.setDriveMediaType(drive, mediaUnknown);

  m_diskCylinders[drive] = cylinders;
  m_diskHeads[drive]     = heads;
  m_diskSectors[drive]   = sectors;
  
  m_diskChanged[drive]   = true;
#if 1
  if (filename) {
    char *filepath = new char[strlen(m_baseDir) + strlen(filename) + 2];
    sprintf(filepath, "%s/%s", m_baseDir, filename);
    m_diskFilename[drive] = strdup(filename);
    m_disk[drive] = fopen(filepath, "r+b");
    if (m_disk[drive]) {

      // get image file size
      fseek(m_disk[drive], 0L, SEEK_END);
      m_diskSize[drive] = ftell(m_disk[drive]);

      // need to detect geometry?
      if (cylinders == 0 || heads == 0 || sectors == 0)
        autoDetectDriveGeometry(drive);
    }
    delete[] filepath;
  }
#else
if (filename) {
  char *filepath = new char[strlen(m_baseDir) + strlen(filename) + 2];
  sprintf(filepath, "%s/%s", m_baseDir, filename);

  m_diskFilename[drive] = strdup(filename);
  m_disk[drive] = fopen(filepath, "r+b");

  if (m_disk[drive]) {

    // 1) Get image size
    fseek(m_disk[drive], 0L, SEEK_END);
    m_diskSize[drive] = ftell(m_disk[drive]);

    // 2) Base geometry (optional, but keep it for sanity)
    if (cylinders == 0 || heads == 0 || sectors == 0) {
      autoDetectDriveGeometry(drive);
    }

    // 3) Read BPB geometry (for partition VBR or superfloppy)
    uint16_t bpb_spt = 0, bpb_heads = 0;
    bool bpbOk = readBPBGeometryFromImage(m_disk[drive], bpb_spt, bpb_heads);

    printf("drive %d BPB: SPT=%u HEADS=%u | emu: SPT=%u HEADS=%u CYL=%u (BPB ok=%d)\n",
           drive, bpb_spt, bpb_heads,
           m_diskSectors[drive], m_diskHeads[drive], m_diskCylinders[drive],
           bpbOk ? 1 : 0);

    // 4) If HDD and BPB is valid, adopt BPB geometry so DOS and BIOS agree
    if (drive >= 2 && bpbOk &&
        bpb_spt >= 1 && bpb_spt <= 63 &&
        bpb_heads >= 1 && bpb_heads <= 255) {

      m_diskSectors[drive] = (uint8_t)bpb_spt;
      m_diskHeads[drive]   = (uint8_t)bpb_heads;

      uint32_t totalSectors = (uint32_t)(m_diskSize[drive] / 512ULL);
      uint32_t denom = (uint32_t)m_diskHeads[drive] * (uint32_t)m_diskSectors[drive];
      uint32_t cyl = (totalSectors + denom - 1) / denom;  // ceil
      if (cyl == 0) cyl = 1;
      if (cyl > 1024) cyl = 1024;

      m_diskCylinders[drive] = (uint16_t)cyl;

      printf("drive %d ADOPTED BPB -> emu: SPT=%u HEADS=%u CYL=%u\n",
             drive, m_diskSectors[drive], m_diskHeads[drive], m_diskCylinders[drive]);
    }

  } else {
    printf("setDriveImage(%d): fopen failed for %s\n", drive, filepath);
  }

  delete[] filepath;
}
#endif
}

void Computer::autoDetectDriveGeometry(int drive)
{
  static const struct {
    uint16_t tracks;
    uint8_t  sectors;
    uint8_t  heads;
  } floppyFormats[] = {
    { 40,  8, 1 },  //  163840 bytes (160K, 5.25 inch)
    { 40,  9, 1 },  //  184320 bytes (180K, 5.25 inch)
    { 40,  8, 2 },  //  327680 bytes (320K, 5.25 inch)
    { 40,  9, 2 },  //  368640 bytes (360K, 5.25 inch)
    { 80,  9, 2 },  //  737280 bytes (720K, 3.5 inch)
    { 80, 15, 2 },  // 1228800 bytes (1200K, 5.25 inch)
    { 80, 18, 2 },  // 1474560 bytes (1440K, 3.5 inch)
    { 80, 36, 2 },  // 2949120 bytes (2880K, 3.5 inch)
  };

  // Look for well known floppy formats
  for (auto const & ff : floppyFormats) {
    if (512 * ff.tracks * ff.sectors * ff.heads == m_diskSize[drive]) {
      m_diskCylinders[drive] = ff.tracks;
      m_diskHeads[drive]     = ff.heads;
      m_diskSectors[drive]   = ff.sectors;
      return;
    }
  }

#if 0
  // Maybe an hard disk, try to calculate geometry
  // Max 528 MB, common lower end for BIOS and MSDOS: https://tldp.org/HOWTO/Large-Disk-HOWTO-4.html
  constexpr int maxCylinders = 1024; // Cylinders : 1...1024
  constexpr int maxHeads     = 16;   // Heads     : 1...16 (actual limit is 256)
  constexpr int maxSectors   = 63;   // Sectors   : 1...63
  int c = 1, h = 1;
  int s = (int)(m_diskSize[drive] / 512);
  if (s > maxSectors) {
    h = s / maxSectors;
    s = maxSectors;
  }
  if (h > maxHeads) {
    c = h / maxHeads;
    h = maxHeads;
  }
  if (c > maxCylinders)
    c = maxCylinders;
  m_diskCylinders[drive] = c;
  m_diskHeads[drive]     = h;
  m_diskSectors[drive]   = s;
#else
  // A classic BIOS-friendly geometry for HDD images
  // This avoids CHS mismatches with DOS/FAT tools
  const uint32_t totalSectors = (uint32_t) (m_diskSize[drive] / 512);

  uint16_t heads   = 16;
  uint8_t  sectors = 63;

  // Cylinders = ceil(totalSectors / (heads * sectors))
  uint32_t cylinders = (totalSectors + (heads * sectors) - 1) / (heads * sectors);
  if (cylinders == 0) {
    cylinders = 1;
  } else if (cylinders > 1024) {
    cylinders = 1024; //TODO
  }

  m_diskCylinders[drive] = (uint16_t) cylinders;
  m_diskHeads[drive]     = (uint8_t) heads;
  m_diskSectors[drive]   = (uint8_t) sectors;
#endif
}

void Computer::setCOM1(SerialPort *serialPort)
{
  m_COM1.setCallbacks(this, COM1Interrupt);
  m_COM1.setSerialPort(serialPort);
}

void Computer::setCOM2(SerialPort *serialPort)
{
  m_COM2.setCallbacks(this, COM2Interrupt);
  m_COM2.setSerialPort(serialPort);
}

void Computer::reset()
{
  printf("computer: Reset\n");
  m_reset = false;
  m_ticksCounter = 0;
  m_speakerDataEnable = false;

  // Default: IBM PC compatible
  //s_memory[0xFFFFE] = 0xFF;

#if LEGACY_IBMPC_XT_8088
  m_i8255.reset();
#else
  m_i8042.reset();
#endif

  m_DMA.reset();

  m_PIC_master.reset();
  m_PIC_slave.reset();

  m_PIT.reset();
  m_PIT.setGate(0, true);
  m_PIT.setGate(1, true);
  m_PIT.setGate(2, m_speakerDataEnable);

  m_MC146818.reset();
  
  m_COM1.reset();
  m_COM2.reset();

  m_video.adapter()->reset();
  m_speakerDataEnable = false;
  if (m_adlib)
    m_adlib->reset();

  m_BIOS.reset();

  i8086::reset();

  // set boot drive (0, 1, 0x80, 0x81)
  i8086::setDL((m_bootDrive & 1) | (m_bootDrive > 1 ? 0x80 : 0x00));
}

void Computer::run()
{
  xTaskCreatePinnedToCore(&runTask, "", 4000, this, 5, &m_taskHandle, fabgl::CoreUsage::quietCore());
}

void Computer::runTask(void *pvParameters)
{
  auto m = (Computer *) pvParameters;

  m->init();

	while (true) {

    if (m->m_reset) {
      m->reset();
    } else if (m->m_paused) {
#if LEGACY_IBMPC_XT_8088
      //TODO m->m_i8255.tick();
#else
      // Replaces tick() for host-only key detection,
      // needed for capturing Ctrl+F4 to resume the system when paused.
      m->m_i8042.tickHostOnly();
#endif
      vTaskDelay(1);
    } else {

#ifdef FABGL_EMULATED
      taskEmuCheck();
      if (m->m_stepCallback)
        m->m_stepCallback(m);
#endif

      i8086::step();
      m->tick();
    }
  }
}

void Computer::PIT_IRQ0(void *context, int timer, bool out)
{
  auto m = (Computer *) context;

  // Timer 0: System timer interrupt (IRQ0 / INT 08h)
  // A rising edge on OUT0 (when OUT0 transitions from 0 to 1) triggers IRQ0
  if ((timer == 0) && out) {
    m->m_PIC_master.signalInterrupt(0);
  }
}

void Computer::tick()
{
  m_ticksCounter++;

  if ((m_ticksCounter & 0x03) == 0x03) {
  
    if (m_COM1.assigned())
      m_COM1.tick();
    if (m_COM2.assigned())
      m_COM2.tick();

    if ((m_ticksCounter & 0x7f) == 0x7f) {
      m_PIT.tick();
#if LEGACY_IBMPC_XT_8088
      m_i8255.tick();
#else
      // run keyboard controller every PIT tick (just to not overload CPU with continous checks)
      m_i8042.tick();
#endif
    }
    
  }

  if (m_PIC_master.pendingInterrupt() && i8086::IRQ(m_PIC_master.pendingInterruptNum()))
    m_PIC_master.ackPendingInterrupt();

  if (m_PIC_slave.pendingInterrupt() && i8086::IRQ(m_PIC_slave.pendingInterruptNum()))
    m_PIC_slave.ackPendingInterrupt();
}

void Computer::writePort(void *context, int address, uint8_t value)
{
  auto m = (Computer *) context;

  switch (address) {

    // 8237A DMA Controller
    case 0x000 ... 0x00F:
      m->m_DMA.write(address, value);
      break;

    // PIC i8259 master
    case 0x20:
    case 0x21:
      m->m_PIC_master.write(address & 1, value);
      break;

    // PIC i8259 slave
    case 0xa0:
    case 0xa1:
      m->m_PIC_slave.write(address & 1, value);
      break;

    // PIT i8253
    case 0x0040: // Timer 0
    case 0x0041: // Timer 1
    case 0x0042: // Timer 2
    case 0x0043: // Control
      m->m_PIT.write(address & 3, value);
      if ((address == 0x43 && (value >> 6) == 2) || address == 0x42) {
        // Set speaker frequency
        int timerCount = m->m_PIT.timerInfo(2).reload;
        if (timerCount == 0)
          timerCount = 65536;
        int freq = PIT_TICK_FREQ / timerCount;
        m->m_waveGen.setFrequency(freq);
      }
      break;

#if LEGACY_IBMPC_XT_8088
    case 0x0060: // Port A
      m->m_i8255.write(0, value);
      break;

    case 0x0061:
    {
      uint8_t old = m->m_i8255.read(1);
      m->m_i8255.write(1, value);            // Port B latch
      m->m_i8255.onPort61Write(old, value);  // PB7 toggle = keyboard ACK

      // Speaker
      const bool gate = value & 0x01;
      const bool out  = value & 0x02;
      m->m_speakerDataEnable = out;
      m->m_PIT.setGate(2, gate);
      if (gate && out) {
        m->m_waveGen.enable(true);
      } else {
        m->m_waveGen.enable(false);
      }
      break;
    }

    case 0x0062: // Port C
      m->m_i8255.write(2, value);
      break;

    case 0x0063: // Control
      m->m_i8255.write(3, value);
      break;

    // 0x0064 (AT 8042) -> not used in XT mode
#else
    // 8042 Keyboard controller input
    case 0x0060:
      m->m_i8042.write(0, value);
      break;

    // Port B (System Control)
    // bit 1 : Speaker data enable
    // bit 0 : Timer 2 gate
    case 0x0061:
    {
      const bool gate = value & 0x01; // Bit 0 controls PIT Counter 2 Gate
      const bool out  = value & 0x02; // Bit 1 controls Speaker Data
      // Note: Bits 2-3 are for parity checks
      m->m_speakerDataEnable = out;
      m->m_PIT.setGate(2, gate);
      if (gate && out) {
        m->m_waveGen.enable(true);
      } else {
        m->m_waveGen.enable(false);
      }
      // Save value
      m->m_dipPB = value;
    }

    // This is NOT part of the 8042 standard,
    // it is an extension of the chipset in many compatible PCs (not IBM).
    case 0x0062:
      break;

    // 8042 Keyboard controller input
    case 0x0064:
      m->m_i8042.write(1, value);
      break;
#endif

    // System Control Register / Video Memory Control
    // bit 7 6 5 4 3 2 1 0
    //     | | | | | | | +- [0] Video RAM enable / map
    //     | | | | | | +--- [1] Video RAM select (bank / size)
    //     | | | | | +----- [2] CPU / video memory arbitration
    //     | | | | +------- [3] Memory wait states
    //     +-+-+-+--------- [4-7] Reserved
    case 0x0065:
      m->m_systemControl = value;
      printf("computer: System control = 0x%02x (port 0x65)\n", value);
      break;

    // MC146818 RTC & RAM
    case 0x0070:
    case 0x0071:
      m->m_MC146818.write(address & 1, value);
      break;

    // 8237A DMA Page Registers (XT)
//    case 0x0080 ... 0x008F:
//      m->m_DMA.write(address, value);

    // GLaBIOS uses OUT 80h,AL to output POST codes
    case 0x0080:
      printf("computer: BIOS POST code = 0x%02x\n", value);
      break;

    // Joystick
    case 0x0201:
      m->m_gameport.start();
      return;

    // COM2
    case 0x02F8 ... 0x02FF:
      if (m->m_COM2.assigned())
        m->m_COM2.write(address, value);
      break;

    // COM3/COM4
    case 0x03E8 ... 0x03EF: // COM3
    case 0x02E8 ... 0x02EF: // COM4
      // writing FCR (+2) or other regs: ignore unless you emulate UARTs 3/4
      break;

    // LPT DATA/CONTROL writes
    //TODO case 0x03BC: case 0x03BE: // MDA LPT
    case 0x0378: case 0x037A: // LPT1
    case 0x0278: case 0x027A: // LPT2
      // ignore
      break;

#if USE_SOUNDBLASTER
    // MIDI - MPU-401
    case 0x0330:
      break;
#endif

    // AdLib (YM3812) ports
    case 0x0388:
    case 0x0389:
      if (m->m_adlib)
        m->m_adlib->write((uint16_t) address, value);
      break;

    case 0x03B0 ... 0x03BF: // Hercules video cards
    case 0x03C0 ... 0x03CF: // EGA video cards
    case 0x03D0 ... 0x03DF: // CGA, EGA and Tandy video cards
      m->m_video.adapter()->writePort(address, value);
      break;

    // FDC
    case 0x03F0 ... 0x03F7:
      m->m_FDC.write(address, value);
      break;

    // COM1
    case 0x03F8 ... 0x03FF:
      if (m->m_COM1.assigned())
        m->m_COM1.write(address, value);
      break;

    // BIOS Olivetti? (for their own HW)
    //case 0x02f0 ... 0x02ff:
    case 0x02f0 ... 0x02f7:
    case 0x06f0 ... 0x06ff:
      break;

    // I/O expander - Configuration
    case EXTIO_CONFIG:
      m->m_MCP23S17.setINTActiveHigh(value & EXTIO_CONFIG_INT_POLARITY);
      break;

    // I/O expander - Port A/B Direction
    case EXTIO_DIRA ... EXTIO_DIRB:
      m->m_MCP23S17.setPortDir(address - EXTIO_DIRA + MCP_PORTA, ~value);
      printf("dir %d = %02X\n", address - EXTIO_DIRA + MCP_PORTA, ~value);
      break;

    // I/O expander - Port A/B pullup
    case EXTIO_PULLUPA ... EXTIO_PULLUPB:
      m->m_MCP23S17.enablePortPullUp(address - EXTIO_PULLUPA + MCP_PORTA, value);
      break;

    // I/O expander - Port A/B write
    case EXTIO_PORTA ... EXTIO_PORTB:
      m->m_MCP23S17.writePort(address - EXTIO_PORTA + MCP_PORTA, value);
      printf("set %d = %02X\n", address - EXTIO_PORTA + MCP_PORTA, value);
      break;

    // I/O expander - GPIO selection
    case EXTIO_GPIOSEL:
      m->m_MCP23S17Sel = value & 0xf;
      break;

    // I/O expander - GPIO direction and pullup
    case EXTIO_GPIOCONF:
      m->m_MCP23S17.configureGPIO(m->m_MCP23S17Sel, value & 1 ? fabgl::MCPDir::Output : fabgl::MCPDir::Input, value & 2);
      break;

    // I/O expander - GPIO write
    case EXTIO_GPIO:
      m->m_MCP23S17.writeGPIO(m->m_MCP23S17Sel, value);
      break;

    default:
      printf("computer: Unhandled write port (%04x=%02x)\n", address, value);
      break;
  }
}

uint8_t Computer::readPort(void *context, int address)
{
  auto m = (Computer *) context;

  switch (address) {

    // 8237A DMA Controller
    case 0x000 ... 0x00F:
      return m->m_DMA.read(address);

    // PIC i8259 master
    case 0x0020:
    case 0x0021:
      return m->m_PIC_master.read(address & 1);

    // PIC i8259 slave
    case 0x00a0:
    case 0x00a1:
      return m->m_PIC_slave.read(address & 1);

    // PIT i8253
    case 0x0040: // Timer 0
    case 0x0041: // Timer 1
    case 0x0042: // Timer 2
    // This is only and input register
    // case 0x0043:
      return m->m_PIT.read(address & 3);

#if LEGACY_IBMPC_XT_8088
  case 0x0060: // Port A (keyboard)
    return m->m_i8255.read(0);

  case 0x0061: // compose read as you already do + latch bits
  {
    uint8_t latchedB = m->m_i8255.read(1);
    uint8_t v = 0;
    v |= (m->m_PIT.getOut(2) ? 0x20 : 0x00);     // bit 5 : Timer 2 out
    v |= (esp_timer_get_time() & 0x10) ? 0x10:0; // bit 4 : Tefresh toggle
    v |= (m->m_speakerDataEnable ? 0x02 : 0x00); // bit 1 : Speaker enable
    v |= (m->m_PIT.getGate(2) ? 0x01 : 0x00);    // bit 0 : Gate
    v |= (latchedB & 0xCC);                      // keep PB7..PB6 and PB3..PB2
    return v;
  }

  case 0x0062: // Port C (DIP logic included)
    return m->m_i8255.read(2);

  case 0x0063: // not readable on a real 8255
    return 0xFF;

  case 0x0064: // not used in XT
    return 0xFF;
#else
    // 8042 keyboard controller output
    case 0x0060:
      return m->m_i8042.read(0);

    // PPI Port B
    //   bit 5 : timer 2 out
    //   bit 4 : toggles every 15.085us (DMA refresh)
    //   bit 1 : speaker data enable
    //   bit 0 : timer 2 gate
    case 0x0061:
      return ((int) m->m_PIT.getOut(2) << 5)     | // bit 5
             (esp_timer_get_time() & 0x10)       | // bit 4 (toggles every 16us) //TODO
             ((int) m->m_speakerDataEnable << 1) | // bit 1
             ((int) m->m_PIT.getGate(2));          // bit 0

    // PPI Port C (System Status / Configuration)
    case 0x0062:
    {
      // Timer 2 output
      const uint8_t timer2Out = 0x20 * m->m_PIT.getOut(2);

      // SW Configuration
      // bit 7 6 5 4 3 2 1 0
      //     | | | | | | | +- [0] Boot from Floppy
      //     | | | | | | +--- [1] Math Coprocessor
      //     | | | | +-+----- [2,3] 11 = 640K RAM
      //     | | +-+--------- [4,5] 10 = CGA 80x25
      //     +-+------------- [6,7] 01 = 2 floppy drives

      // Bit 3 of port 0x61 (PBSW) selects which part of SW to read:
      //   0 = read SW bits 0-3, 1 = read SW bits 4-7
      if (!(m->m_dipPB & 0x08)) { 
        return timer2Out | 0x0C; // 640 KB RAM
      } else {
        return timer2Out | 0x06; // CGA 80x15 and 2 floppies
      }
      break;
    }

    // 8042 keyboard controller status register
    case 0x0064:
      return m->m_i8042.read(1);
#endif

    // MC146818 RTC & RAM
    case 0x0070:
    case 0x0071:
      return m->m_MC146818.read(address & 1);

    // 8237A DMA Page Registers (XT)
//    case 0x0080 ... 0x008F:
//      m->m_DMA.read(address);
      break;

    // Joystick
    case 0x0201:
      return m->m_gameport.read();

    // COM2
    case 0x02F8 ... 0x02FF:
      return m->m_COM2.assigned() ? m->m_COM2.read(address) : 0;

    // COM3/COM4 IIR (offset +2)
    case 0x03EA: // COM3 IIR
    case 0x02EA: // COM4 IIR
      return 0xFF;

    // LPT DATA reads
    //TODO case 0x03BC: // MDA LPT data
    case 0x0378: // LPT1 data
      return 0x00;

    case 0x0379: // LPT1 status
      return 0x90; // busy=0, select=1, ack=0 (safe dummy)

    case 0x037A: // LPT1 control
      return 0x0C; // init=1, select=1 (safe)

    case 0x0278: // LPT2 data
    case 0x0279:
    case 0x027A:
      return 0xFF;

//#if USE_SOUNDBLASTER
    // MIDI - MPU-401
    case 0x0331:
      return 0xFF;
//#endif

    // AdLib (YM3812) ports
    case 0x0388:
    case 0x0389:
      if (m->m_adlib)
        return m->m_adlib->read((uint16_t) address);
      return 0xFF;

    case 0x03B0 ... 0x03BF: // Hercules
    case 0x03C0 ... 0x03CF: // EGA
    case 0x03D0 ... 0x03DF: // CGA, EGA and Tandy
      return m->m_video.adapter()->readPort(address);

    // FDC
    case 0x03F0 ... 0x03F7:
      return m->m_FDC.read(address);

    // COM1
    case 0x03F8 ... 0x03FF:
      return m->m_COM1.assigned() ? m->m_COM1.read(address) : 0;

    // I/O expander - Configuration
    case EXTIO_CONFIG:
      return (m->m_MCP23S17.available()        ? EXTIO_CONFIG_AVAILABLE    : 0) |
             (m->m_MCP23S17.getINTActiveHigh() ? EXTIO_CONFIG_INT_POLARITY : 0);

    // I/O expander - Port A/B Direction
    case EXTIO_DIRA ... EXTIO_DIRB:
      return m->m_MCP23S17.getPortDir(address - EXTIO_DIRA + MCP_PORTA);

    // I/O expander - Port A/B pullup
    case EXTIO_PULLUPA ... EXTIO_PULLUPB:
      return m->m_MCP23S17.getPortPullUp(address - EXTIO_PULLUPA + MCP_PORTA);

    // I/O expander - Port A/B read
    case EXTIO_PORTA ... EXTIO_PORTB:
      return m->m_MCP23S17.readPort(address - EXTIO_PORTA + MCP_PORTA);

    // I/O expander - GPIO selection
    case EXTIO_GPIOSEL:
      return m->m_MCP23S17Sel;

    // I/O expander - GPIO read
    case EXTIO_GPIO:
      return m->m_MCP23S17.readGPIO(m->m_MCP23S17Sel);
  }

  static uint16_t lastPort = 0;
  if (address != lastPort) {
    printf("computer: Unhandled read port (%04x)\n", address);
  }
  lastPort = address;
  return 0xFF;
}

void Computer::writeVideoMemory8(void *context, int address, uint8_t value)
{
  auto m = (Computer *) context;
  m->m_video.adapter()->writeMem8(address, value);
}

void Computer::writeVideoMemory16(void *context, int address, uint16_t value)
{
  auto m = (Computer *) context;
  m->m_video.adapter()->writeMem16(address, value);
}

uint8_t Computer::readVideoMemory8(void *context, int address)
{
  auto m = (Computer *) context;
  return m->m_video.adapter()->readMem8(address);
}

uint16_t Computer::readVideoMemory16(void *context, int address)
{
  auto m = (Computer *) context;
  return m->m_video.adapter()->readMem16(address);
}

bool Computer::interrupt(void *context, int num)
{
  auto m = (Computer *) context;

  switch (num) {
/*
    case 0x05:
      // Default BIOS behavior: Print Screen OR BOUND handler (empty)
      // For HAS_FPU and instruction tests, simply ignore safely.
      printf("computer: int 5h\n");
      return true;
*/
    // Note that, if the INT 08h does not perform a real IRET, the IF flag remains at 0,
    // and then no other IRQs will be accepted (including IRQ1 from the keyboard).
    case 0x08: // System Timer Tick Count (increase)
    case 0x1c: // User Timer
      return false;

    // Keyboard Hardware Interrupt (IRQ1)
    case 0x09:
      return false;

    // BIOS Video
    case 0x10:
      m->m_video.adapter()->handleInt10h();
      return true;

    // BIOS Equipment List
    case 0x11:
      m->m_BIOS.handleInt11h();
      m->printEquipmentWord();
      return true;

    // Conventional Memory Size
    case 0x12:
      m->m_BIOS.handleInt12h();
      return true;

    // BIOS disk handler (INT 13h)
    case 0x13:
      m->m_BIOS.handleInt13h();
      return true;

    // Serial Port BIOS Services
    case 0x14:
      m->m_BIOS.handleInt14h();
      return true;

    // BIOS Extended Services
    case 0x15:
      m->m_BIOS.handleInt15h();
      return true;

    // BIOS Keyboard Services
    case 0x16:
      //m->m_BIOS.handleInt16h();
      return false;

    // BIOS Printer Services
    case 0x17:
      m->m_BIOS.handleInt17h();
      return true;

    // Bootstrap Loader
    case 0x19:
      printf("computer: int19h (Bootstrap)\n");
      if (!m->m_BIOS.handleInt19h())
        m->interrupt(m, 0x18);
      return true;

    // BIOS Time of Day / RTC Service
    case 0x1A:
      m->m_BIOS.handleInt1Ah();
      return true;

    // DOS
    case 0x20 ... 0x2F:
      return false;

    // 8087/80287 floating‑point exception vectors (34h-3Ch)
    // Reserved / unused system vectors (30h-33h, 3Dh-3Fh)
    case 0x30 ... 0x3F:
      return false;

    // User defined
    case 0x60 ... 0x66:
      return false;

    default:
      printf("computer: Unhandled interrupt 0x%02x\n", num);
      return false;
  }
}

// interrupt from MC146818, trig 8259B-IR0 (IRQ8, INT 70h)
bool Computer::MC146818Interrupt(void *context)
{
  auto m = (Computer *) context;
  return m->m_PIC_slave.signalInterrupt(0);
}

// interrupt from COM1, trig 8259A-IR4 (IRQ4, INT 0Ch)
bool Computer::COM1Interrupt(i8250 *source, void *context)
{
  auto m = (Computer *) context;
  return source->getOut2() && m->m_PIC_master.signalInterrupt(4);
}

// interrupt from COM2, trig 8259A-IR3 (IRQ3, INT 0Bh)
bool Computer::COM2Interrupt(i8250 *source, void *context)
{
  auto m = (Computer *) context;
  return source->getOut2() && m->m_PIC_master.signalInterrupt(3);
}

// 8259A-IR1 (IRQ1, INT 09h)
bool Computer::keyboardInterrupt(void *context)
{
  auto m = (Computer *) context;
  return m->m_PIC_master.signalInterrupt(1);
}

// 8259B-IR4 (IRQ12, INT 074h)
bool Computer::mouseInterrupt(void *context)
{
  auto m = (Computer *) context;
  return m->m_PIC_slave.signalInterrupt(4);
}

// Reset from 8042 (Ctrl + Alt + Del)
bool Computer::triggerReset(void *context)
{
  auto m = (Computer *) context;
  m->m_reset = true;
  return true;
}

// SysReq (Alt + Print Screen)
void Computer::hostReq(void *context, uint8_t reqId)
{
  auto m = (Computer *) context;
  if (m->m_sysReqCallback)
    m->m_sysReqCallback(reqId);
}

void Computer::audio_toggleMute()
{
  if (m_soundGen.playing()) {
    m_soundGen.play(false);
    m_video.showVolume(0);
    printf("computer: Speaker muted\n");
  } else {
    const uint8_t vol = m_soundGen.volume();
    m_soundGen.play(true);
    m_video.showVolume(vol);
    printf("computer: Set volume = %d\n", vol);
  }
}

void Computer::audio_volumeUp()
{
  uint8_t vol = m_soundGen.volume();
  vol += 5;
  if (vol > 127) {
    vol = 127;
  }
  m_soundGen.setVolume(vol);
  m_video.showVolume(vol);
  printf("computer: Set volume = %d\n", vol);
}

void Computer::audio_volumeDown()
{
  uint8_t vol = m_soundGen.volume();
  if (vol > 5) {
    vol -= 5;
  } else {
    vol = 0;
  }
  m_soundGen.setVolume(vol);
  m_video.showVolume(vol);
  printf("computer: Set volume = %d\n", vol);
}

void Computer::video_snapshot(const char *path)
{
  uint8_t *framebuffer;
  uint16_t width;
  uint16_t height;

  pause();

  framebuffer = m_video.rawSnapshot(&width, &height);

  resume();

  if (framebuffer) {
    int rc = snapshot(width, height, framebuffer, path);
    heap_caps_free((void *) framebuffer);
  }
}

void Computer::printEquipmentWord()
{
  // Read the 16-bit value from BDA at 0040:0010h
  uint16_t eq = *(uint16_t *) &s_memory[0x410];

  printf("computer: Equipment List (0x%04x)\n", eq);

  // Video Mode (bits 4-5)
  uint8_t videoMode = (eq >> 4) & 0x03;
  const char *videoDesc = "";
  switch(videoMode) {
    case 0x00: videoDesc = "(EGA/VGA)"; break;
    case 0x01: videoDesc = "(40x25 Color)"; break;
    case 0x02: videoDesc = "(80x25 Color)"; break;
    case 0x03: videoDesc = "(80x25 Mono)"; break;
  }
  printf("computer: Video mode = %d %s\n", videoMode, videoDesc);

  // Floppy Info (bit 0 and bits 6-7)
  bool hasFloppy = (eq & 0x01) != 0;
  printf("computer: Floppy drive = %s\n", hasFloppy ? "yes" : "no");
  if (hasFloppy) {
    printf("computer: Floppy count = %d\n", ((eq >> 6) & 0x03) + 1);
  }

  // Math Coprocessor (bit 1)
  printf("computer: Math coprocessor (FPU) = %s\n", (eq & 0x02) ? "yes" : "no");

  // RAM Size (bits 2-3) - Old BIOS standard
  // 00=16K, 01=32K, 10=48K, 11=64K (not used by modern BIOS, but part of the word)
  printf("computer: Base RAM bank = %d\n", (eq >> 2) & 0x03);

  // Game Adapter (bit 12)
  printf("computer: Game adapter = %s\n", (eq & 0x1000) ? "yes" : "no");

  // Serial Ports (bits 9-11)
  printf("computer: Serial ports (COM) = %d\n", (eq >> 9) & 0x07);

  // Parallel Ports (bits 14-15)
  printf("computer: Parallel ports (LPT) = %d\n", (eq >> 14) & 0x03);
}

void Computer::dumpMemory(char const *filename)
{
  constexpr int BLOCKLEN = 1024;

  char *filepath = new char[strlen(m_baseDir) + strlen(filename) + 2];
  sprintf(filepath, "%s/%s", m_baseDir, filename);
  auto file = fopen(filepath, "wb");
  if (file) {
    for (int i = 0; i < 1048576; i += BLOCKLEN)
      fwrite(s_memory + i, 1, BLOCKLEN, file);
    fclose(file);
  }
  delete[] filepath;
}

void Computer::dumpInfo(char const *filename)
{
  char *filepath = new char[strlen(m_baseDir) + strlen(filename) + 2];
  sprintf(filepath, "%s/%s", m_baseDir, filename);
  auto file = fopen(filepath, "wb");
  if (file) {
    // CPU state
    fprintf(file, " CS   DS   ES   SS\n");
    fprintf(file, "%04X %04X %04X %04X\n\n", i8086::CS(), i8086::DS(), i8086::ES(), i8086::SS());
    fprintf(file, " IP   AX   BX   CX   DX   SI   DI   BP   SP\n");
    fprintf(file, "%04X %04X %04X %04X %04X %04X %04X %04X %04X\n\n", i8086::IP(), i8086::AX(), i8086::BX(), i8086::CX(), i8086::DX(), i8086::SI(), i8086::DI(), i8086::BP(), i8086::SP());
    fprintf(file, "O D I T S Z A P C\n");
    fprintf(file, "%d %d %d %d %d %d %d %d %d\n\n", i8086::flagOF(), i8086::flagDF(), i8086::flagIF(), i8086::flagTF(), i8086::flagSF(), i8086::flagZF(), i8086::flagAF(), i8086::flagPF(), i8086::flagCF());
    fprintf(file, "CS+IP: %05X\n", i8086::CS() * 16 + i8086::IP());
    fprintf(file, "SS+SP: %05X\n\n", i8086::SS() * 16 + i8086::SP());
    fclose(file);
  }
  delete[] filepath;
}
