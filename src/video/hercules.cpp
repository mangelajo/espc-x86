/*
 * Copyright (c) 2026 Jesus Martinez-Mateo
 *
 * Author: Jesus Martinez-Mateo <jesus.martinez.mateo@gmail.com>
 *
 * This file is part of a GPL-licensed project.
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

#include "video/hercules.h"
#include "video/video_scanout.h"

#include "video/cga_palette.h"

#include "core/i8086.h"
#include "bios.h"

#include <stdio.h>
#include <string.h>

#pragma GCC optimize ("O3")

// Hercules (HGC) Ports Bits
/*
#define HGC_MODECONTROLREG_GRAPHICSPAGE   0x80   // 0 = graphics mapped on page 0 (0xB0000), 1 = graphics mapped on page 1 (0xB8000)

#define HGC_CONFSWITCH_ALLOWGRAPHICSMODE  0x01   // 0 = prevents graphics mode, 1 = allows graphics mode
#define HGC_CONFSWITCH_ALLOWPAGE1         0x02   // 0 = prevents access to page 1, 1 = allows access to page 1
*/

using fabgl::i8086; // CPU register access

namespace video {

HGC::HGC() :
  m_textAttr(0x07) // light gray on black
{
  m_vram = nullptr;
}

HGC::~HGC()
{
  m_video->stop();

  if (m_vram) {
    heap_caps_free((void *) m_vram);
    m_vram = nullptr;
  }
}

void HGC::init(uint8_t *ram, VideoScanout *video)
{
  // External resources
  s_ram = ram;
  m_video = video;

  // Allocate video memory
  if (!m_vram) {
    printf("hgc: Allocating video memory (%d KB)\n", HGC_VRAM_SIZE / 1024);
    // Allocate video memory in DRAM (with DMA)
    m_vram = (uint8_t*) heap_caps_malloc(HGC_VRAM_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!m_vram) {
      printf("hgc: Not enough DRAM!!\n");
    }
  }

  m_video->setSource(this);

  reset();
}

void HGC::reset()
{
  m_video->stop();

  // Clear video memory
  memset(m_vram, 0, HGC_VRAM_SIZE);

  // Initialize video card registers and state values
  initRegisters();

  // Set video mode and clear screen
  setMode(MDA_MODE_TEXT_80x25_MONO);

  // Update BIOS Data Area
  s_ram[0x449] = m_currentMode;
  syncBDA();

  m_video->run();
}

void HGC::initRegisters()
{
  // Clear CRTC Registers
  memset(m_crtc, 0, sizeof(m_crtc));
  m_crtcIndex = 0;

  // Default mode control and config switch
  m_modeControl = HGC_MC_VIDEO_ENABLE;
  m_confSwitch = HGC_CS_ALLOW_GRAPHICS | HGC_CS_ALLOW_PAGE1;

  m_startAddress = 0;
  m_statusQuery = 0;

  // Cursor Shape and Visible
  m_cursorDisable = false;
  m_cursorStart = 0x0D;
  m_cursorEnd = 0x0F;

  m_crtc[HGC_CRTC_CURSORSTART] = m_cursorStart | (m_cursorDisable ? 0x20 : 0x00);
  m_crtc[HGC_CRTC_CURSOREND]   = m_cursorEnd;

  m_activePage = 0;

  // Cursor Position
  for (int i = 0; i < 8; i++) {
    m_cursorRow[i] = 0;
    m_cursorCol[i] = 0;
  }
}

// --- INT 10h ---

void HGC::handleInt10h()
{
  const uint8_t AH = i8086::AH();

  switch (AH) {

    // Set Video Mode
    case 0x00:
    {
      uint8_t mode = i8086::AL() & 0x0F;

      if (mode < 0x04) { // CGA text mode
        printf("hgc: Unsupported (text) video mode 0x%02x (force 0x07)\n", mode);
        mode = MDA_MODE_TEXT_80x25_MONO;
      } else if ((mode != MDA_MODE_TEXT_80x25_MONO) &&
                 (mode != MDA_MODE_GFX_720x348_MONO)) {
        printf("hgc: Unsupported (graphics) video mode 0x%02x (force 0x0F)\n", mode);
        mode = MDA_MODE_GFX_720x348_MONO;
      }

      // Hercules only officially supports: AL = 07h (MDA text)
      switch(mode) {

        case MDA_MODE_TEXT_80x25_MONO:
          m_modeControl &= ~HGC_MC_GRAPHICS_MODE;
          m_modeControl |= HGC_MC_VIDEO_ENABLE;
          break;

        case MDA_MODE_GFX_720x348_MONO:
          if (allowGraphics()) {
            m_currentMode = MDA_MODE_GFX_720x348_MONO;
            m_modeControl |= HGC_MC_GRAPHICS_MODE;
            m_modeControl |= HGC_MC_VIDEO_ENABLE;
          } else {
            m_currentMode = MDA_MODE_TEXT_80x25_MONO;
            m_modeControl &= ~HGC_MC_GRAPHICS_MODE;
            m_modeControl |= HGC_MC_VIDEO_ENABLE;
          }
          break;

        default:
          printf("hgc: Unexpected mode 0x%02x\n", mode);
          return;
      }

      m_video->stop();

      setMode(mode);
      clearScreen();

      // Update BIOS Data Area
      s_ram[0x449] = m_currentMode;
      syncBDA();

      m_video->run();
      break;
    }

    // Set Cursor Shape
    case 0x01:
    {
      const uint8_t start = i8086::CH();
      const uint8_t end   = i8086::CL();

      // CRTC Registers
      m_crtc[HGC_CRTC_CURSORSTART] = start;
      m_crtc[HGC_CRTC_CURSOREND]   = end;

      // Internals
      m_cursorStart = start & 0x1F;
      m_cursorEnd   = end   & 0x1F;
      m_cursorDisable = (start & 0x20) != 0; // bit 5 disables cursor

      // Update BDA
      s_ram[0x460] = m_cursorStart;
      s_ram[0x461] = m_cursorEnd;

      m_dirty = true;
      break;
    }

    // Set Cursor Position
    case 0x02:
      m_cursorRow[m_activePage] = i8086::DH();
      m_cursorCol[m_activePage] = i8086::DL();

      // Update CRTC registers and BDA
      syncCursorPos();
      m_dirty = true;
      break;

    // Read Cursor Position and Shape
    case 0x03:
    {
      const uint8_t page = i8086::BH() & 0x07;

      // Cursor Shape
      i8086::setCH(m_cursorStart);
      i8086::setCL(m_cursorEnd);

      // Cursor Position
      i8086::setDH(m_cursorRow[page]);
      i8086::setDL(m_cursorCol[page]);
      break;
    }

    // Read Light Pen Position
    case 0x04:
      i8086::setFlagCF(true); // Unsupported
      break;

    // Select Active Display Page
    case 0x05:
    {
      const uint8_t page = i8086::AL() & 0x07;

      m_activePage = page;

      // Start addres (in words)
      const uint16_t oldAddr = m_startAddress;
      m_startAddress = ((uint16_t) page * m_textPageSize) >> 1;
      if (m_startAddress != oldAddr) {
        printf("hgc: Start address = 0x%04x (active page %d)\n", m_startAddress, m_activePage);
      }

      const uint8_t addr_hi = (uint8_t) ((m_startAddress >> 8) & 0xFF);
      const uint8_t addr_lo = (uint8_t) ( m_startAddress       & 0xFF);

      // Update BDA
      s_ram[0x44E] = addr_hi;
      s_ram[0x44F] = addr_lo;
      s_ram[0x462] = page;

      // Update CRTC Start Address
      m_crtc[HGC_CRTC_STARTADDR_HI] = addr_hi;
      m_crtc[HGC_CRTC_STARTADDR_LO] = addr_lo;

      syncCursorPos();
      m_dirty = true;
      break;
    }

    // Scroll Up Window
    case 0x06:
    {
      const uint8_t lines  = i8086::AL(); // lines to scroll (0 => clear window)
      const uint8_t attr   = i8086::BH(); // fill attribute
      const uint8_t top    = i8086::CH(); // top row
      const uint8_t left   = i8086::CL(); // left col
      const uint8_t bottom = i8086::DH(); // bottom row
      const uint8_t right  = i8086::DL(); // right col

      // Only in text modes, in graphics mode ignore
      if (!isGraphicsMode()) {
        scrollUpWindow(lines, attr, top, left, bottom, right);
      }
      break;
    }

    // Scroll Down Window
    case 0x07:
    {
      const uint8_t lines  = i8086::AL(); // lines to scroll (0 => clear window)
      const uint8_t attr   = i8086::BH(); // fill attribute
      const uint8_t top    = i8086::CH(); // top row
      const uint8_t left   = i8086::CL(); // left col
      const uint8_t bottom = i8086::DH(); // bottom row
      const uint8_t right  = i8086::DL(); // right col

      // Only in text modes, in graphics mode ignore
      if (!isGraphicsMode()) {
        scrollDownWindow(lines, attr, top, left, bottom, right);
      }
      break;
    }

    // Read Character and Attribute at Cursor
    case 0x08:
    {
      const uint8_t page = i8086::BH() & 0x07;

      // Get cursor position for the selected page
      const uint8_t row = m_cursorRow[page];
      const uint8_t col = m_cursorCol[page];

      // Calculate VRAM offset for this cell
      const uint32_t offset = textCellOffset(page, row, col);

      // Read character and attribute from VRAM
      i8086::setAL(m_vram[offset + 0]); // character
      i8086::setAH(m_vram[offset + 1]); // attribute
      break;
    }

    // Write Character and Attribute at Cursor (Repeat)
    case 0x09:
    {
      const uint8_t ch     = i8086::AL(); // Character
      const uint8_t attr   = i8086::BL(); // Attribute
      const uint8_t page   = i8086::BH() & 0x07;
      const uint16_t count = i8086::CX(); // # of repetitions

      // Only in text modes, in graphics mode ignore
      if (!isGraphicsMode()) {
        writeCharAttr(ch, attr, count, page);
      }
      break;
    }

    // Write Character Only at Cursor (Repeat)
    case 0x0A:
    {
      const uint8_t ch     = i8086::AL(); // Character
      const uint8_t page   = i8086::BH() & 0x07;
      const uint16_t count = i8086::CX(); // # of repetitions

      // Only in text modes, in graphics mode ignore
      if (!isGraphicsMode()) {
        writeCharOnly(ch, count, page);
      }
      break;
    }

/*
    // Write Pixel — no estándar en MDA/HGC BIOS, pero lo soportamos si se solicita en modo gráfico
    case 0x0C:
      if (m_currentMode == HGC_MODE_GRAPHICS_720x348) {
        const uint16_t x = i8086::CX();
        const uint16_t y = i8086::DX();
        const bool on    = (i8086::AL() & 0x01) != 0;
        if (x < (uint16_t)m_width && y < (uint16_t)m_height)
          writePixelHGC_720(x, y, on);
      }
      break;
*/
    // Teletype Output
    case 0x0E:
    {
      const uint8_t ch    = i8086::AL();
      const uint8_t page  = i8086::BH() & 0x07;
      const uint8_t color = i8086::BL(); // Optional foreground color

      // Only in text modes, in graphics mode ignore
      if (!isGraphicsMode()) {
        // Write character, advance cursor, wrap/scroll
        ttyOutput(ch, page, color);
      }
      break;
    }

    default:
      printf("hgc: Unhandled int 10h (AH=0x%02x)\n", AH);
      break;
  }
}

// --- I/O Ports ---

uint8_t HGC::readPort(uint16_t port)
{
  switch (port) {

    // CRTC Index
    case HGC_PORT_CRTC_INDEX:
      return m_crtcIndex;

    // CRTC Data
    case HGC_PORT_CRTC_DATA:
      return m_crtc[m_crtcIndex];

    // Mode Control Register
    case HGC_PORT_MODECTL:
      return m_modeControl;

    // Status Register
    // This register is used by the BIOS to distinguish between:
    // - MDA : status bit never changes
    // - HGC : status bit toggles over time
    case HGC_PORT_STATUS:
    case 0x03DA:
#if 0
      // Fake status: alterna VSYNC a ritmo constante.
      // Bit 7 VSYNC alto aproximadamente 1/8 lecturas.
      m_statusQuery++;
      return (m_statusQuery & 0x7) ? 0x00 : 0x80;
#else
    {
      // Increment a read counter on every access
      m_statusQuery++;

      uint8_t status = 0x00;

      // Bit 7: Hercules sync / vertical retrace indicator
      // Toggle it every 8 reads so it is clearly non-constant
      if ((m_statusQuery & 0x07) == 0) {
        status |= 0x80;
      }

      // Optional: make bit 0 "alive" as well, to avoid software
      // expecting some activity in the status register.
      status |= (m_statusQuery & 0x01);

      return status;
    }
#endif

    case HGC_PORT_LPT_DATA:
      // Return the last byte written to the data latch
      return m_lptData;

    case HGC_PORT_LPT_STATUS:
      // Returns the current status of the printer hardware
      // 0xDF (11011111b):
      // Bit 7: Busy (1 = Not busy, signal is inverted)
      // Bit 5: Paper Out (0 = Paper present)
      // Bit 4: Selected (1 = Printer is online)
      // Bit 3: Error (1 = No error)
      return 0xDF;

    case HGC_PORT_LPT_CONTROL:
      // Return the control bits (0-4). Bits 5-7 usually read as 1.
      return m_lptControl | 0xE0;

    case HGC_PORT_CONF_SWITCH:
      return m_confSwitch;

    default:
      printf("hgc: Unhandled read (0x%04x)\n", port);
      return 0xFF;
  }
}

void HGC::writePort(uint16_t port, uint8_t value)
{
  switch (port) {

    // CRTC Index
    case HGC_PORT_CRTC_INDEX:
      m_crtcIndex = value & 0x1F;
      break;

    // CRTC Data
    case HGC_PORT_CRTC_DATA:
      m_crtc[m_crtcIndex] = value;
      switch (m_crtcIndex) {

        case 0x00: // Horizontal Total
        case 0x01: // Horizontal Displayed
        case 0x02: // Horizontal Sync Position
        case 0x03: // Sync Width
        case 0x04: // Vertical Total
        case 0x05: // Vertical Total Adjust
        case 0x06: // Vertical Displayed
        case 0x07: // Vertical Sync Position
          // Nothing to do
          break;

        case 0x08: // Interlace Mode
          // bit 7 6 5 4 3 2 1 0
          //     | | | | | | +-+- [0,1] Interface Mode
          //     | | | | +-+----- [2,3] Reserved
          //     | | +-+--------- [4,5] Display Skew
          //     +-+------------- [6,7] Cursor Skew
          // Interface Mode : 00, 10 = Non-interlace mode, 01 = Interlace Mode, 11 = Interlace & sync
          // Skew : 00 = No delay, 01 = 1-char delay, 10 = 2-char delay, 11 = Not available
          break;

        case 0x09: // Maximum Scan Line Address
          // bit 7 6 5 4 3 2 1 0
          //     | | | +-+-+-+-+- [0-4] Max Scan Line
          //     +-+-+----------- [5-7] Reserved
          // Max Scan Line
          // - Text mode: character height - 1
          // - Graphics mode: number of 8 KB banks
          break;

        // Cursor Start
        case HGC_CRTC_CURSORSTART:
          // bits 0..4 : Cursor start scanline
          // bit  5    : Cursor disable
          m_cursorStart = value & 0x1F;
          m_cursorDisable = (value & 0x20) != 0;
          m_dirty = true;
          break;

        // Cursor End
        case HGC_CRTC_CURSOREND:
          // bits 0..4 : Cursor end scanline
          m_cursorEnd = value & 0x1F;
          m_dirty = true;
          break;

        // Start Address High and Low
        case HGC_CRTC_STARTADDR_HI:
        case HGC_CRTC_STARTADDR_LO:
        {
          const uint16_t addr_hi = (uint16_t) m_crtc[HGC_CRTC_STARTADDR_HI] << 8;
          const uint16_t addr_lo = (uint16_t) m_crtc[HGC_CRTC_STARTADDR_LO];
          const uint16_t oldAddr = m_startAddress;
          m_startAddress = addr_hi | addr_lo;
          if (m_startAddress != oldAddr) {
            printf("hgc: Start address = 0x%04x\n", m_startAddress);
          }
          m_dirty = true;
          break;
        }

        // Cursor Position High and Low
        case HGC_CRTC_CURSORPOS_HI:
        case HGC_CRTC_CURSORPOS_LO:
        {
          const uint16_t pos_hi = (uint16_t) m_crtc[HGC_CRTC_CURSORPOS_HI] << 8;
          const uint16_t pos_lo = (uint16_t) m_crtc[HGC_CRTC_CURSORPOS_LO];
          // Note that (pos_hi | pos_lo) is the absolute cursor address (in chars)
          const uint16_t cursorPos = (pos_hi | pos_lo) - m_startAddress;

          m_cursorRow[m_activePage] = (uint8_t) (cursorPos / m_textCols);
          m_cursorCol[m_activePage] = (uint8_t) (cursorPos % m_textCols);
          m_dirty = true;
          break;
        }

        default:
          printf("hgc: Unhandled crtc[0x%02x]=0x%02x\n", m_crtcIndex, value);
          break;
      }
      break;

    // Mode Control Register
    // bit 7 6 5 4 3 2 1 0
    //     | | | | | | | +- [0] Reserved
    //     | | | | | | +--- [1] Graphics (0=text mode, 1=graphics mode)
    //     | | | | | +----- [2] Reserved
    //     | | | | +------- [3] Video Enabled (0=disable, 1=enable)
    //     | | | +--------- [4] Reserved
    //     | | +----------- [5] Blink (0=bit 7 high intens., 1=bit 7 blinking)
    //     | +------------- [6] Reserved
    //     +--------------- [7] Page Select (0=page 0 0xB0000, 1=page 1 0xB8000)
    case HGC_PORT_MODECTL:
    {
      uint8_t mode;
      m_modeControl = value;
      if (isGraphicsMode() && allowGraphics()) {
        mode = MDA_MODE_GFX_720x348_MONO;
      } else {
        mode = MDA_MODE_TEXT_80x25_MONO;
        m_modeControl &= ~HGC_MC_GRAPHICS_MODE;
      }
      if (mode != m_currentMode) {
        printf("hgc: Mode control = 0x%02x (%d)\n", value, isVideoEnabled());
        m_video->stop();
        setMode(mode);
        //if (isVideoEnabled())
          m_video->run();
      }
      break;
    }

    case HGC_PORT_LPT_DATA:
      // Store byte to be "sent" to printer
      m_lptData = value;
      break;

    case HGC_PORT_LPT_CONTROL:
      // Bits 0-3 control strobe, autofeed, init, and select.
      // Bit 4: IRQ Enable (usually IRQ 7)
      m_lptControl = value & 0x1F;
      break;

    case HGC_PORT_CONF_SWITCH:
      printf("hgc: Config switch = 0x%02x\n", value);
      m_confSwitch = value;
      //setMode();
      break;

    default:
      printf("hgc: Unhandled write (0x%04x=0x%02x)\n", port, value);
      break;
  }
}

// --- Video Memory ---

uint8_t HGC::readMem8(uint32_t physAddr)
{
  if ((physAddr >= HGC_VRAM_BASE) && (physAddr <= HGC_VRAM_LIMIT)) {
    const uint32_t addr = physAddr - HGC_VRAM_BASE;
    // When upper 32K is disabled, filter 0xB8000–HGC_VRAM_LIMIT
    if (!allowPage1() && (addr >= HGC_PAGE1_OFFSET))
      return 0xFF;
    return m_vram[addr];
  }
  return 0xFF;
}

uint16_t HGC::readMem16(uint32_t physAddr)
{
  const uint16_t word_lo = (uint16_t) readMem8(physAddr);
  const uint16_t word_hi = (uint16_t) readMem8(physAddr + 1) << 8;
  return word_hi | word_lo;
}

void HGC::writeMem8(uint32_t physAddr, uint8_t value)
{
  if ((physAddr >= HGC_VRAM_BASE) && (physAddr <= HGC_VRAM_LIMIT)) {
    const uint32_t addr = physAddr - HGC_VRAM_BASE;
    if (!allowPage1() && (addr >= HGC_PAGE1_OFFSET))
      return; // Ignore when upper 32K is disabled
    m_vram[addr] = value;
    m_dirty = true;
  }
}

void HGC::writeMem16(uint32_t physAddr, uint16_t value)
{
  writeMem8(physAddr,     (uint8_t) value & 0xFF);
  writeMem8(physAddr + 1, (uint8_t) (value >> 8) & 0xFF);
}

// --- Helpers ---

void HGC::setMode(uint8_t mode)
{
  m_currentMode = mode;
  switch(m_currentMode) {

    case MDA_MODE_TEXT_80x25_MONO:
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = 0x1000; // 4096
      break;

    case MDA_MODE_GFX_720x348_MONO:
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = 0x1000; // 4096
      break;

    default:
      printf("hgc: Unexpected mode (0x%02x)\n", m_currentMode);
      return;
  }

  m_video->setMode(m_currentMode);
}

void HGC::writeCharAttr(uint8_t ch, uint8_t attr, uint16_t count, uint8_t page)
{
  if (attr == 0) {
    attr = m_textAttr; // When no attribute defined use default
  }

  // Compute target cell offset and write character + attribute
  uint32_t offset = textCellOffset(page, m_cursorRow[page], m_cursorCol[page]);

  // Write 'count' copies of (ch, attr), without advancing cursor
  for (uint16_t i = 0; i < count; i++) {
    m_vram[offset++] = ch;
    m_vram[offset++] = attr;
  }

  // No cursor position updated
  m_dirty = true;
}

void HGC::writeCharOnly(uint8_t ch, uint16_t count, uint8_t page)
{
  while (count--) {
    // Compute VRAM offset
    uint32_t offset = textCellOffset(page, m_cursorRow[page], m_cursorCol[page]);

    // Write only character, DO NOT change attribute
    m_vram[offset] = ch;

    // Advance cursor
    m_cursorCol[page]++;
    if (m_cursorCol[page] >= m_textCols) {
      newLine(page);
    }
  }

  syncCursorPos();
  m_dirty = true;
}

// Scrolls a rectangular text area UP
void HGC::scrollUpWindow(uint8_t lines, uint8_t attr,
                         uint8_t top, uint8_t left,     // Top-left corner
                         uint8_t bottom, uint8_t right) // Bottom-right corner
{
  if (attr == 0) {
    attr = m_textAttr; // When no attribute defined use default
  }

  // Clamp boundaries
  if (bottom >= m_textRows) bottom = m_textRows - 1;
  if (right  >= m_textCols) right  = m_textCols - 1;
  if (top > bottom || left > right)
      return;

  const size_t lineSize = m_textCols * 2; // bytes per row
  const size_t base = (size_t) m_activePage * m_textPageSize;

  const uint8_t rectCols = right - left + 1;
  const uint8_t rectRows = bottom - top + 1;
  const size_t rectBytes = (size_t) rectCols * 2;

  // When AL=0: clear the whole rectangle
  if (lines == 0) {
      lines = rectRows;
  } else if (lines > rectRows) {
      lines = rectRows;
  }

  if (lines == rectRows) {
    // Clear the entire area
    for (int r = top; r <= bottom; r++) {
      size_t offset = base + r * lineSize + left * 2;
      for (int c = 0; c < rectCols; c++) {
        m_vram[offset + c*2    ] = 0x20; // space
        m_vram[offset + c*2 + 1] = attr; // attribute
      }
    }
  } else {
    // Scroll upward: shift rows top to bottom-lines
    for (int r = top; r <= bottom - lines; r++) {
      size_t dst = base + r * lineSize + left * 2;
      size_t src = base + (r + lines) * lineSize + left * 2;
      memmove(m_vram + dst, m_vram + src, rectBytes);
    }

    // Fill the bottom 'lines' rows with blanks
    for (int r = bottom - lines + 1; r <= bottom; r++) {
      size_t offset = base + r * lineSize + left * 2;
      for (int c = 0; c < rectCols; c++) {
        m_vram[offset + c*2    ] = 0x20;
        m_vram[offset + c*2 + 1] = attr;
      }
    }
  }
  m_dirty = true;
}

// Scrolls a rectangular text area DOWN
void HGC::scrollDownWindow(uint8_t lines, uint8_t attr,
                           uint8_t top, uint8_t left,     // Top-left corner
                           uint8_t bottom, uint8_t right) // Bottom-right corner
{
  if (attr == 0) {
    attr = m_textAttr; // When no attribute defined use default
  }

  // Clamp boundaries
  if (bottom >= m_textRows) bottom = m_textRows - 1;
  if (right  >= m_textCols) right  = m_textCols - 1;
  if (top > bottom || left > right)
    return;

  const size_t lineSize = m_textCols * 2; // bytes per line
  const size_t base = (size_t) m_activePage * m_textPageSize;

  const uint8_t rectCols = right - left + 1;
  const uint8_t rectRows = bottom - top + 1;
  const size_t rectBytes = (size_t) rectCols * 2;

  // When AL=0: clear the whole rectangle
  if (lines == 0) {
    lines = rectRows;
  } else if (lines > rectRows) {
    lines = rectRows;
  }

  if (lines == rectRows) {
    // Clear the entire area
    for (int r = top; r <= bottom; r++) {
      size_t offset = base + r * lineSize + left * 2;
      for (int c = 0; c < rectCols; c++) {
        m_vram[offset + c*2    ] = 0x20; // space
        m_vram[offset + c*2 + 1] = attr; // attribute
      }
    }
  } else {
    // Move existing lines down inside the rectangle:
    // Bottom-to-top order is required due to overlap
    for (int r = bottom; r >= top + lines; --r) {
      const size_t dst = base + r * lineSize + left * 2;
      const size_t src = base + (r - lines) * lineSize + left * 2;
      memmove(m_vram + dst, m_vram + src, rectBytes);
    }

    // Fill the top 'lines' rows with blanks
    for (int r = top; r < top + lines; r++) {
      const size_t offset = base + r * lineSize + left * 2;
      for (int c = 0; c < rectCols; c++) {
        m_vram[offset + c*2    ] = 0x20;
        m_vram[offset + c*2 + 1] = attr;
      }
    }
  }
  m_dirty = true;
}

void HGC::ttyOutput(uint8_t ch, uint8_t page, uint8_t color)
{
  uint8_t attr = m_textAttr;
  if (color != 0) {
    // Use color as foreground
    attr = (attr & 0xF0) | (color & 0x0F);
  }

  switch (ch) {

    case 0x07: // BEL - no speaker here, ignore
      break;

    case 0x08: // Back Space
      if (m_cursorCol[page] > 0)
        m_cursorCol[page]--;
      break;

    case 0x09: // Tabular - next 8-column stop
    {
      uint8_t next = (uint8_t) ((m_cursorCol[page] + 8) & ~7);
      m_cursorCol[page] = (next < m_textCols) ? next : (m_textCols - 1);
      break;
    }

    case 0x0A: // Line Feed
      newLine(page);
      break;

    case 0x0D: // Carriage Return
      m_cursorCol[page] = 0;
      break;

    default:
    {
      if (ch < 0x20) {
        break; // No printable character
      }

      const uint32_t offset = textCellOffset(page, m_cursorRow[page], m_cursorCol[page]);

      m_vram[offset    ] = ch;   // text glyph
      m_vram[offset + 1] = attr; // attribute

      m_cursorCol[page]++;
      if (m_cursorCol[page] >= m_textCols)
        newLine(page);
      break;
    }
  }

  syncCursorPos();
  m_dirty = true;
}

void HGC::writeString(uint8_t flags, uint8_t page, uint8_t attr,
                      uint8_t row, uint8_t col,
                      uint16_t ES, uint16_t BP, uint16_t len)
{
  // INT 10h/AH=13h - Write String (text modes)
  // bit 0 : update cursor after write
  // bit 1 : use BL as attribute (if bit2=0)
  // bit 2 : string contains (char, attr) pairs
  const bool updateCursor = (flags & 0x01) != 0;
  const bool useBLAttr    = (flags & 0x02) != 0;
  const bool hasAttrBytes = (flags & 0x04) != 0;

  // Clamp start position
  if (row >= m_textRows) row = (uint8_t)(m_textRows - 1);
  if (col >= m_textCols) col = (uint8_t)(m_textCols - 1);

  // Save original cursor for this page
  // (some callers request "do not update cursor")
  const uint8_t savedRow = m_cursorRow[page];
  const uint8_t savedCol = m_cursorCol[page];

  // Use the page cursor as a working position
  m_cursorRow[page] = row;
  m_cursorCol[page] = col;

  // Fixed attribute when the string does not include attributes
  const uint8_t fixedAttr = useBLAttr ? attr : m_textAttr;

  // ES:BP -> linear physical address
  uint32_t phys = ((uint32_t) ES << 4) + (uint32_t) BP;

  if (hasAttrBytes) {
    // Each element is (char, attr), len is number of characters
    while (len--) {
      const uint8_t ch = s_ram[phys++];
      const uint8_t at = s_ram[phys++];

      const uint32_t offset = textCellOffset(page, m_cursorRow[page], m_cursorCol[page]);
      m_vram[offset + 0] = ch;
      m_vram[offset + 1] = at;

      // Advance cursor position with wrapping and scrolling
      m_cursorCol[page]++;
      if (m_cursorCol[page] >= m_textCols)
        newLine(page);
    }
  } else {
    // Each element is char only, attribute is fixed (BL or default)
    while (len--) {
      const uint8_t ch = s_ram[phys++];

      const uint32_t offset = textCellOffset(page, m_cursorRow[page], m_cursorCol[page]);
      m_vram[offset + 0] = ch;
      m_vram[offset + 1] = fixedAttr;

      // Advance cursor position with wrapping and scrolling
      m_cursorCol[page]++;
      if (m_cursorCol[page] >= m_textCols)
        newLine(page);
    }
  }

  // Apply or restore cursor depending on bit 0
  if (updateCursor) {
    // If writing to the active page, also update CRTC/BDA via helper
    if (page == m_activePage) {
      syncCursorPos();
    } else {
      // Update BDA cursor position for the specified page only
      s_ram[0x450 + 2 * page    ] = m_cursorCol[page];
      s_ram[0x450 + 2 * page + 1] = m_cursorRow[page];
    }
  } else {
    // Restore original cursor for this page
    m_cursorRow[page] = savedRow;
    m_cursorCol[page] = savedCol;
  }

  m_dirty = true;
}

void HGC::newLine(uint8_t page)
{
  m_cursorCol[page] = 0;
  m_cursorRow[page]++;
  if (m_cursorRow[page] >= m_textRows) {
    // Scroll the page and clamp the cursor to the last row.
    scrollUp(page);
    m_cursorRow[page] = m_textRows - 1;
  }
}

void HGC::scrollUp(uint8_t page)
{
  // Move lines 1..N-1 to 0..N-2 within the given page and clear the last line
  const uint32_t lineSize = (uint32_t) m_textCols * 2;
  const uint32_t base     = (uint32_t) page * m_textPageSize;

  memmove(m_vram + base,
          m_vram + base + lineSize,
          (size_t)((m_textRows - 1) * lineSize));

  // Clear last line with spaces using the current attribute
  uint32_t lastLine = base + (uint32_t) (m_textRows - 1) * lineSize;
  for (uint32_t i = 0; i < (uint32_t) m_textCols; i++) {
    m_vram[lastLine + i * 2 + 0] = 0x20;       // Space character
    m_vram[lastLine + i * 2 + 1] = m_textAttr; // Default attribute
  }
}

void HGC::clearScreen()
{
  // Clear video memory
  memset(m_vram, 0, HGC_VRAM_SIZE);

  if (!isGraphicsMode()) {
    const uint16_t base = (uint16_t) m_activePage * m_textPageSize;

    for (uint8_t row = 0; row < m_textRows; row++) {
      for (uint8_t col = 0; col < m_textCols; col++) {
        const uint16_t offset = base + ((uint16_t) row * m_textCols + col) * 2;
        m_vram[offset + 0] = 0x20;       // Space
        m_vram[offset + 1] = m_textAttr; // Default attribute
      }
    }
  }
  m_dirty = true;
}

void HGC::syncBDA()
{
  // Set Equipment Word: Bits 4-5 must be 11 (binary) for Monochrome
  // 0x30 = 0011 0000 in binary. This tells the software: "I am an MDA/Hercules"
  //*(uint16_t *) &s_ram[0x410] = (s_ram[0x410] & 0xFFCF) | 0x0030;

  // s_ram[0x449] = 0x03;
  *(uint16_t *) &s_ram[0x44A] = m_textCols;      // Columns
  *(uint16_t *) &s_ram[0x44C] = m_textPageSize;  // Page size (bytes)
  *(uint16_t *) &s_ram[0x44E] = m_startAddress << 1;  // Start Address
  // Only for debug
  if ((m_startAddress << 1) != (m_activePage * m_textPageSize)) {
  	printf("hgc: Start address mismatch (page=%d, size=%d, addr=%d)\n",
	  m_activePage, m_textPageSize, m_startAddress << 1);
  }

  // Cursor Positions
  for (int i = 0; i < 8; i++) {
    s_ram[0x450 + 2*i  ] = m_cursorCol[i];     // Cursor Column
    s_ram[0x450 + 2*i+1] = m_cursorRow[i];     // Cursor Row
  }
  s_ram[0x460] = m_cursorStart;                // Cursor Shape
  s_ram[0x461] = m_cursorEnd;
  s_ram[0x462] = m_activePage;                 // Active Page
  *(uint16_t *) &s_ram[0x463] = 0x03B4;          // CRTC Base Port
  s_ram[0x465] = m_modeControl;                // Mode Control Register
  //TODO s_ram[0x466] = m_colorSelect;                // Color Select Register
}

void HGC::syncCursorPos()
{
  // Update cursor position in CRTC registers
  uint16_t pos = (uint16_t) m_cursorRow[m_activePage] * m_textCols +
                            m_cursorCol[m_activePage];
//                            m_cursorCol[m_activePage] + m_startAddress;

  m_crtc[HGC_CRTC_CURSORPOS_HI] = (uint8_t) ((pos >> 8) & 0xFF);
  m_crtc[HGC_CRTC_CURSORPOS_LO] = (uint8_t) ( pos       & 0xFF);

  // Update BDA
  s_ram[0x450 + 2*m_activePage  ] = m_cursorCol[m_activePage];
  s_ram[0x450 + 2*m_activePage+1] = m_cursorRow[m_activePage];
}

void HGC::writePixelHGC_720(uint16_t x, uint16_t y, bool on)
{
  if (x >= 720 || y >= 348)
    return;

  const uint32_t pageBase = (isPage1Active() && allowPage1()) ? HGC_PAGE1_OFFSET : HGC_PAGE0_OFFSET;

  const uint32_t rowGroup   = (y & 3u) * 0x2000u;       // banco (0, 0x2000, 0x4000, 0x6000)
  const uint32_t rowOffset  = (uint32_t(y) >> 2) * 90u; // 90 bytes por scanline efectiva
  const uint32_t byteOffset = (uint32_t(x) >> 3);       // x / 8
  const uint32_t offs       = pageBase + rowGroup + rowOffset + byteOffset;

  const uint8_t  bit  = 7u - (x & 7u);
  const uint8_t  mask = uint8_t(1u << bit);

  if (on)
    m_vram[offs] |=  mask;
  else
    m_vram[offs] &= ~mask;

  m_dirty = true;
}

} // end of namespace
