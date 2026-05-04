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

#include "video/mcga.h"
#include "video/video_scanout.h"

#include "core/i8086.h"

#include <stdio.h>
#include <string.h>

#pragma GCC optimize ("O3")

#define MCGA_TEXT_40x25_PAGE_SIZE  0x0800 // 2048
#define MCGA_TEXT_80x25_PAGE_SIZE  0x1000 // 4096

#define MCGA_VRAM_BANK_SIZE        0x2000

using fabgl::i8086; // CPU register access

namespace video {

MCGA::MCGA() :
  m_textAttr(0x07) // light gray on black
{
  m_vram = nullptr;
}

MCGA::~MCGA()
{
  m_video->stop();

  if (m_vram) {
    heap_caps_free((void *) m_vram);
    m_vram = nullptr;
  }
}

void MCGA::init(uint8_t *ram, VideoScanout *video)
{
  // External resources
  s_ram = ram;
  m_video = video;

  // Allocate video memory
  if (!m_vram) {
    printf("mcga: Allocating video memory (%d KB)\n", MCGA_VRAM_SIZE / 1024);
    // Allocate video memory in DRAM (with DMA)
    m_vram = (uint8_t *) heap_caps_malloc(MCGA_VRAM_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!m_vram) {
      printf("mcga: Not enough DRAM!!\n");
    }
  }

  initPalette();

  m_video->setSource(this);

  reset();
}

void MCGA::reset()
{
  m_video->stop();

  // Initialize video card registers and state values
  resetRegisters();

  // Set video mode and clear screen
  setMode(CGA_MODE_TEXT_80x25_16COLORS);

  // Update BIOS Data Area
  s_ram[0x449] = m_currentMode;
  syncBDA();

  m_video->run();
}

void MCGA::initPalette()
{
  // --- PART 1: Standard CGA colors (0-15) ---
  // Mapping the 16 classic colors using your provided CGA_palette
  for (int i = 0; i < 16; i++) {
    MCGA_palette[i] = CGA_palette[i];
  }

  // --- PART 2: Grayscale Ramp (16-31) ---
  // MCGA has 16 levels of gray. We compress them into 4 physical levels (0-3).
  for (int i = 0; i < 16; i++) {
    uint8_t gray_level = i / 4; // Scale 0-15 down to 0-3
    MCGA_palette[i + 16] = RGB222(gray_level, gray_level, gray_level);
  }

  // --- PART 3: Color Rainbow/Gradient (32-255) ---
  // The original MCGA palette uses a mix of hue, saturation, and luminance.
  // To fit this into 6 bits (RGB222), we use a mathematical distribution.
  for (int i = 32; i < 256; i++) {
    // We extract bits to cover the 64 possible combinations in the VGA32
    // while filling all 256 slots of the MCGA virtual palette.
    uint8_t r = ((i - 32) >> 4) & 0x03; // Red component (0-3)
    uint8_t g = ((i - 32) >> 2) & 0x03; // Green component (0-3)
    uint8_t b = (i - 32) & 0x03;        // Blue component (0-3)
        
    MCGA_palette[i] = RGB222(r, g, b);
  }
}

void MCGA::resetRegisters()
{
  // Clear CRTC Registers
  memset(m_crtc, 0, sizeof(m_crtc));
  m_crtcIndex = 0;

  // Default mode control and color set
  m_modeControl = MCGA_MC_ENABLED |
                  MCGA_MC_TEXT80COLS |
                  MCGA_MC_BIT7BLINK;
  m_colorSelect = MCGA_DEFAULT_COLORSELECT;

  m_VSyncQuery = 0;
  m_startAddress = 0;

  // Cursor Shape and Visible
  m_cursorDisable = false;
#if 0
  m_cursorStart = 0x0D;
  m_cursorEnd = 0x0F;
#else
  m_cursorStart = 0x06;
  m_cursorEnd = 0x07;
#endif

  m_crtc[MCGA_CRTC_CURSORSTART] = m_cursorStart | (m_cursorDisable ? 0x20 : 0x00);
  m_crtc[MCGA_CRTC_CURSOREND]   = m_cursorEnd;

  m_activePage = 0;

  // Cursor Position
  for (int i = 0; i < 8; i++) {
    m_cursorRow[i] = 0;
    m_cursorCol[i] = 0;
  }
}

// --- INT 10h ---

void MCGA::handleInt10h()
{
  const uint8_t AH = i8086::AH();

  switch (AH) {

    // Set Video Mode
    case 0x00:
    {
      // bit 7 6 5 4 3 2 1 0
      //     | +-+-+-+-+-+-+- [0-6] Video Mode
      //     +--------------- [7] Clear Memory Flag (0=clear, 1=don't clear)
      const uint8_t mode = i8086::AL();
      uint8_t videoMode = mode & 0x7F;
      const bool cls = (mode & 0x80) == 0;

      // MDA Text Mode 80x25 (mono) - monochrome ignored by MCGA
      if (videoMode == MDA_MODE_TEXT_80x25_MONO) {
        printf("mcga: Ignoring MDA text video mode (0x%02x)\n", videoMode);
        videoMode = CGA_MODE_TEXT_80x25_16COLORS;
      }

      if (videoMode == m_currentMode) {
        if (cls)
          clearScreen();
        break; // Nothing to do
      }

      switch(videoMode) {

        // Text Mode 40x25
        case CGA_MODE_TEXT_40x25_16COLORS:
        case CGA_MODE_TEXT_40x25_16COLORS_ALT:
          m_modeControl = MCGA_MC_ENABLED |
                          MCGA_MC_BIT7BLINK;
          break;

        // Text Mode 80x25
        case CGA_MODE_TEXT_80x25_16COLORS:
        case CGA_MODE_TEXT_80x25_16COLORS_ALT:
        //case MDA_MODE_TEXT_80x25_MONO:
          m_modeControl = MCGA_MC_ENABLED |
                          MCGA_MC_TEXT80COLS |
                          MCGA_MC_BIT7BLINK;
          break;

         // Graphics Mode 320x200
        case CGA_MODE_GFX_320x200_4COLORS:
        case CGA_MODE_GFX_320x200_4COLORS_ALT:
          m_modeControl = MCGA_MC_ENABLED |
                          MCGA_MC_GRAPHICS |
                          MCGA_MC_BIT7BLINK;
          break;

        // Graphics Mode 640x200
        case CGA_MODE_GFX_640x200_2COLORS:
          m_modeControl = MCGA_MC_ENABLED |
                          MCGA_MC_GRAPHICS |
                          MCGA_MC_HIGHRES |
                          MCGA_MC_BIT7BLINK;
          break;

        // MCGA Graphics Mode 320x200x256
        case MCGA_MODE_GFX_320x200_256COLORS:
          m_modeControl = MCGA_MC_ENABLED |
                          MCGA_MC_GRAPHICS |
                          MCGA_MC_HIGHRES |
                          MCGA_MC_BIT7BLINK;
          break;

        default:
          printf("mcga: Warning! Unexpected video mode (0x%02x)\n", videoMode);
          return;
      }
      m_colorSelect = MCGA_DEFAULT_COLORSELECT;

      // Reset BIOS video state
      m_activePage = 0;
      m_startAddress = 0;
      for (int p = 0; p < 8; p++) {
        m_cursorRow[p] = 0;
        m_cursorCol[p] = 0;
      }

      m_video->stop();

      setMode(videoMode);
      if (cls) {
        clearScreen();
      }

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
      m_crtc[MCGA_CRTC_CURSORSTART] = start;
      m_crtc[MCGA_CRTC_CURSOREND]   = end;

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
      m_startAddress = ((uint16_t) page * m_textPageSize) >> 1;

      const uint8_t addr_hi = (uint8_t) ((m_startAddress >> 8) & 0xFF);
      const uint8_t addr_lo = (uint8_t) ( m_startAddress       & 0xFF);

      // Update BDA
      s_ram[0x44E] = addr_hi;
      s_ram[0x44F] = addr_lo;
      s_ram[0x462] = page;

      // Update CRTC Start Address
      m_crtc[MCGA_CRTC_STARTADDR_HI] = addr_hi;
      m_crtc[MCGA_CRTC_STARTADDR_LO] = addr_lo;

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

    // Set Palette / Background Intensity
    case 0x0B:
    {
      const uint8_t BH = i8086::BH();
      const uint8_t BL = i8086::BL();

      if (BH == 0x00) { // Set Background and Border Color (and Palette Intensity)
        // Clear current color (bits 0-3) and intensity (bit 4)
        m_colorSelect &= ~(MCGA_CS_COLOR_MASK | MCGA_CS_HIGHINTENSITY);

        // bits 0-3 : Set background / border color
        m_colorSelect |= (BL & 0x0F);

        // bit 4 : Set palette intensity (affects colors 1, 2, and 3)
        if (BL & 0x10) {
          m_colorSelect |= MCGA_CS_HIGHINTENSITY;
        }
      } else if (BH == 0x01) { // Select Palette Set
        // BL bit 0 selects the palette:
        //   0 = Palette 0 (Green/Red/Brown)
        //   1 = Palette 1 (Cyan/Magenta/White)
        if (BL & 0x01) {
          m_colorSelect |= MCGA_CS_PALETTESEL;
        } else {
          m_colorSelect &= ~MCGA_CS_PALETTESEL;
        }
      }
      // Apply the new palette settings
      m_video->updateLUT();
      m_video->setBorder(m_colorSelect & 0x0F);
      break;
    }

    // Write Pixel (BIOS AT / Extended)
    case 0x0C:
    // Write Dot (BIOS PC/XT Original, 1981)
    case 0x0D:
      if (isGraphicsMode()) {
        const uint8_t AL = i8086::AL();
        const uint32_t x = i8086::CX();
        const uint32_t y = i8086::DX();

        if (isHighResolution()) { // Graphics Mode 640x200
          const bool on = (AL & 1);
          writePixel640x200(x, y, on);
        } else { // Graphics Mode 320x200
          const uint8_t value = AL & 0x3; // 0b11
          const bool xored = AL & 0x80;
          writePixel320x200(x, y, value);
        }
      }
      break;

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

    // Get Video Mode
    case 0x0F:
      i8086::setAL(m_currentMode);
      i8086::setAH(m_textCols); // Screen width in characters
      i8086::setBH(m_activePage);
      break;

    // Palette Functions
    case 0x10:
    {    
      uint8_t AL = i8086::AL();

      switch (AL) {

        // Set Individual DAC Register
        case 0x10:
        {
          const uint8_t index = i8086::BL();

          m_dacPalette[index][0] = i8086::DH() & 0x3F; // Red component
          m_dacPalette[index][1] = i8086::CH() & 0x3F; // Green component
          m_dacPalette[index][2] = i8086::CL() & 0x3F; // Blue component

          //TODO Convert VGA 6-bit DAC values (0-63) to RGB222 (0-3) used by VGA32
          // with rounding: r2 = round(r6 * 3 / 63)
          const uint8_t r2 = (uint8_t) (((uint16_t) m_dacPalette[index][0] * 3 + 31) / 63);
          const uint8_t g2 = (uint8_t) (((uint16_t) m_dacPalette[index][1] * 3 + 31) / 63);
          const uint8_t b2 = (uint8_t) (((uint16_t) m_dacPalette[index][2] * 3 + 31) / 63);

          // Update the scanout palette (index -> RGB222)
          MCGA_palette[index] = RGB222(r2, g2, b2);
          //TODO m_video->updateLUT();

          i8086::setFlagCF(false);
          break;
        }

        // Set Block of DAC Registers
        case 0x12:
        {
          const uint16_t start = i8086::BX(); // Start index
          const uint16_t count = i8086::CX();

          // ES:DX = pointer to RGB triples (R,G,B), 0-63 each
          const uint16_t ES = i8086::ES();
          const uint16_t DX = i8086::DX();

          uint32_t addr = ((uint32_t) ES << 4) + DX;

          for (uint16_t i = 0; i < count; i++) {
            const uint8_t index = (start + i) & 0xFF; 

            // Read 6-bit VGA DAC values from memory
            m_dacPalette[index][0] = s_ram[addr++] & 0x3F; // Red
            m_dacPalette[index][1] = s_ram[addr++] & 0x3F; // Green
            m_dacPalette[index][2] = s_ram[addr++] & 0x3F; // Blue
          }

          // TODO: convert updated DAC palette to RGB222
          // and rebuild the 256->16 reduction LUT if needed

          i8086::setFlagCF(false);
          break;
        }

        // Read Individual DAC Register
        case 0x15:
        {
          const uint8_t index = i8086::BL();

          i8086::setDH(m_dacPalette[index][0]); // Return Red
          i8086::setCH(m_dacPalette[index][1]); // Return Green
          i8086::setCL(m_dacPalette[index][2]); // Return Blue

          i8086::setFlagCF(false);
          break;
        }

        default:
          i8086::setFlagCF(false);
          printf("mcga: Unhandled int 10h (AH=0x10, AL=0x%02x)\n", AL);
          break;
      }
      break;
    }

    // Character Generator Functions
    case 0x11: 
      i8086::setFlagCF(true);
      break;

    // Info
    case 0x12:
      printf("mcga: int 10h (AH=0x12)\n");
      i8086::setFlagCF(false);
      break;

    // Write String (text modes only)
    case 0x13:
    {
      if (isGraphicsMode()) {
        // Not supported in graphics modes
        i8086::setFlagCF(true);
        break;
      }

      const uint8_t flags = i8086::AL();
      const uint8_t page  = i8086::BH() & 0x07;
      const uint8_t attr  = i8086::BL();
      const uint16_t len  = i8086::CX();
      const uint8_t row   = i8086::DH();
      const uint8_t col   = i8086::DL();

      const uint16_t ES = i8086::ES();
      const uint16_t BP = i8086::BP();

      writeString(flags, page, attr, row, col, ES, BP, len);

      // Should succeed in text mode
      i8086::setFlagCF(false);
      break;
    }

    // Video Display Combination
    case 0x1A:
    {
      const uint8_t AL = i8086::AL();
      if (AL == 0x00) {
        i8086::setAL(0x1A); // echo
        i8086::setBL(0x0C); // MCGA with analog color monitor
        i8086::setBH(0x00);
        i8086::setCH(0x00);
        i8086::setCL(0x00);
        i8086::setFlagCF(false);
      }
      break;
    }

    default:
      printf("mcga: Unhandled int 10h (AH=0x%02x)\n", AH);
      break;
  }
}

// --- I/O Ports ---

uint8_t MCGA::readPort(uint16_t port)
{
//printf("read 0x%04x\n", port);
  switch (port) {

    // CRTC Index
    case MCGA_PORT_CRTCIDX:
      return m_crtcIndex;

    // CRTC Data
    case MCGA_PORT_CRTCDATA:
      return m_crtc[m_crtcIndex];

    // Mode Control Register
    case MCGA_PORT_MODECTRL:
      return m_modeControl;

    // Color Select Register
    case MCGA_PORT_COLORSEL:
      return m_colorSelect;

    // Status Register
    // real vertical sync is too fast for our slowly emulated 8086, so
    // here it is just a fake, just to allow programs that check it to keep going anyway.
    case MCGA_PORT_STATUS:
      m_VSyncQuery++;
      return (m_VSyncQuery & 0x7) != 0 ? 0x09 : 0x00; // "not VSync" (0x00) every 7 queries

    default:
      printf("mcga: Unhandled read (0x%04x)\n", port);
      return 0xFF;
  }
}

void MCGA::writePort(uint16_t port, uint8_t value)
{
  //printf("write 0x%04x = 0x%02x\n", port, value);
  switch (port) {

    // CRTC Index
    case MCGA_PORT_CRTCIDX:
      m_crtcIndex = value & 0x1F;
      break;

    // CRTC Data
    case MCGA_PORT_CRTCDATA:
      m_crtc[m_crtcIndex] = value;
      switch (m_crtcIndex) {

        // Cursor Start
        case MCGA_CRTC_CURSORSTART:
          // bits 0..4 : Cursor start scanline
          // bit  5    : Cursor disable
          m_cursorStart = value & 0x1F;
          m_cursorDisable = (value & 0x20) != 0;
          m_dirty = true;
          break;

        // Cursor End
        case MCGA_CRTC_CURSOREND:
          // bits 0..4 : Cursor end scanline
          m_cursorEnd = value & 0x1F;
          m_dirty = true;
          break;

        // Start Address High and Low
        case MCGA_CRTC_STARTADDR_HI:
        case MCGA_CRTC_STARTADDR_LO:
        {
          const uint16_t addr_hi = (uint16_t) m_crtc[MCGA_CRTC_STARTADDR_HI] << 8;
          const uint16_t addr_lo = (uint16_t) m_crtc[MCGA_CRTC_STARTADDR_LO];
          const uint16_t oldAddr = m_startAddress;
          m_startAddress = addr_hi | addr_lo;
          if (m_startAddress != oldAddr) {
            printf("mcga: Start address = 0x%04x\n", m_startAddress);
          }
          m_dirty = true;
          break;
        }

        // Cursor Position High and Low
        case MCGA_CRTC_CURSORPOS_HI:
        case MCGA_CRTC_CURSORPOS_LO:
        {
          const uint16_t pos_hi = (uint16_t) m_crtc[MCGA_CRTC_CURSORPOS_HI] << 8;
          const uint16_t pos_lo = (uint16_t) m_crtc[MCGA_CRTC_CURSORPOS_LO];
          // Note that (pos_hi | pos_lo) is the absolute cursor address (in chars)
          const uint16_t cursorPos = (pos_hi | pos_lo) - m_startAddress;

          m_cursorRow[m_activePage] = (uint8_t) (cursorPos / m_textCols);
          m_cursorCol[m_activePage] = (uint8_t) (cursorPos % m_textCols);
          m_dirty = true;
          break;
        }

        default:
          printf("mcga: Unhandled crtc[0x%02x]=0x%02x\n", m_crtcIndex, value);
          break;
      }
      break;

    // Mode Control Register
    case MCGA_PORT_MODECTRL:
    {
      uint8_t mode;

      m_modeControl = value;
      if (isGraphicsMode()) {
        mode = isHighResolution() ? CGA_MODE_GFX_640x200_2COLORS
                                  : CGA_MODE_GFX_320x200_4COLORS;
      } else {
        mode = isText80Columns() ? CGA_MODE_TEXT_80x25_16COLORS
                                 : CGA_MODE_TEXT_40x25_16COLORS;
      }
      if (mode != m_currentMode) {
        m_video->stop();
        setMode(mode);
        if (isVideoEnabled())
          m_video->run();
      }
      break;
    }

    // Color Select Register
    case MCGA_PORT_COLORSEL:
      m_colorSelect = value;
      m_video->updateLUT();
      m_video->setBorder(m_colorSelect);
      break;

    default:
      printf("mcga: Unhandled write (0x%04x=0x%02x)\n", port, value);
      break;
  }
}

// --- Video Memory ---

uint8_t MCGA::readMem8(uint32_t physAddr)
{
  if (isGraphicsMode()) {
    // A000h
    if ((physAddr >= MCGA_VRAM_GFX_BASE) && (physAddr <= MCGA_VRAM_GFX_LIMIT))
      return m_vram[physAddr - MCGA_VRAM_GFX_BASE];
  } else {
    // B800h
    if ((physAddr >= MCGA_VRAM_TEXT_BASE) && (physAddr <= MCGA_VRAM_TEXT_LIMIT))
      return m_vram[physAddr - MCGA_VRAM_TEXT_BASE];
  }
  return 0xFF;
}

uint16_t MCGA::readMem16(uint32_t physAddr)
{
  const uint16_t word_lo = (uint16_t) readMem8(physAddr);
  const uint16_t word_hi = (uint16_t) readMem8(physAddr + 1) << 8;
  return word_hi | word_lo;
}

void MCGA::writeMem8(uint32_t physAddr, uint8_t value)
{
  if (isGraphicsMode()) {
    // A000h
    if ((physAddr >= MCGA_VRAM_GFX_BASE) && (physAddr <= MCGA_VRAM_GFX_LIMIT)) {
      m_vram[physAddr - MCGA_VRAM_GFX_BASE] = value;
      m_dirty = true;
    }
  } else {
    // B800h
    if ((physAddr >= MCGA_VRAM_TEXT_BASE) && (physAddr <= MCGA_VRAM_TEXT_LIMIT)) {
      m_vram[physAddr - MCGA_VRAM_TEXT_BASE] = value;
      m_dirty = true;
    }
  }
}

void MCGA::writeMem16(uint32_t physAddr, uint16_t value)
{
  writeMem8(physAddr,     (uint8_t) value & 0xFF);
  writeMem8(physAddr + 1, (uint8_t) (value >> 8) & 0xFF);
}

// --- Helpers ---

void MCGA::setMode(uint8_t mode)
{
  m_currentMode = mode;
  switch(m_currentMode) {

    case CGA_MODE_TEXT_40x25_16COLORS:
    case CGA_MODE_TEXT_40x25_16COLORS_ALT:
      m_textRows = 25;
      m_textCols = 40;
      m_textPageSize = MCGA_TEXT_40x25_PAGE_SIZE;
      break;

    case CGA_MODE_TEXT_80x25_16COLORS:
    case CGA_MODE_TEXT_80x25_16COLORS_ALT:
    case MDA_MODE_TEXT_80x25_MONO:
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = MCGA_TEXT_80x25_PAGE_SIZE;
      break;

    // In graphic modes we also update the number of text rows
    // and cols to answer INT 10h AH=0x0F
    case CGA_MODE_GFX_320x200_4COLORS:
    case CGA_MODE_GFX_320x200_4COLORS_ALT:
      m_textRows = 25;
      m_textCols = 40;
      m_textPageSize = MCGA_TEXT_40x25_PAGE_SIZE;
      break;

    case CGA_MODE_GFX_640x200_2COLORS:
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = MCGA_TEXT_80x25_PAGE_SIZE;
      break;

    case MCGA_MODE_GFX_320x200_256COLORS:
      m_textRows = 25;
      m_textCols = 40;
      m_textPageSize = MCGA_TEXT_40x25_PAGE_SIZE;
      break;

    default:
      // Unreachable
      break;
  }

  m_video->setMode(m_currentMode);
}

void MCGA::writePixel320x200(uint32_t x, uint32_t y, uint8_t value)
{
  const uint32_t offset = y * 320 + x;
  m_vram[offset] = value;
  m_dirty = true;
}

void MCGA::writePixel640x200(uint32_t x, uint32_t y, bool on)
{
  constexpr uint32_t rowLenBytes = 640 / 8;

  const uint32_t offset = (y >> 1) * rowLenBytes   // row
                        + (x >> 3)                 // col
                        + ((y & 1) ? MCGA_VRAM_BANK_SIZE : 0);  // bank

  const uint8_t bit = 7 - (x & 7);
  const uint8_t mask = (1 << bit);

  uint8_t &cell = m_vram[offset];

  if (on) {
    cell |= mask;
  } else {
    cell &= ~mask;
  }
  m_dirty = true;
}

void MCGA::writeCharAttr(uint8_t ch, uint8_t attr, uint16_t count, uint8_t page)
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

void MCGA::writeCharOnly(uint8_t ch, uint16_t count, uint8_t page)
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
void MCGA::scrollUpWindow(uint8_t lines, uint8_t attr,
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
void MCGA::scrollDownWindow(uint8_t lines, uint8_t attr,
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

void MCGA::ttyOutput(uint8_t ch, uint8_t page, uint8_t color)
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

void MCGA::writeString(uint8_t flags, uint8_t page, uint8_t attr,
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

void MCGA::newLine(uint8_t page)
{
  m_cursorCol[page] = 0;
  m_cursorRow[page]++;
  if (m_cursorRow[page] >= m_textRows) {
    // Scroll the page and clamp the cursor to the last row.
    scrollUp(page);
    m_cursorRow[page] = m_textRows - 1;
  }
}

void MCGA::scrollUp(uint8_t page)
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

void MCGA::clearScreen()
{
  // Clear video memory
  memset(m_vram, 0, MCGA_VRAM_SIZE);

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

void MCGA::syncBDA()
{
  // s_ram[0x449] = 0x03;
  *(uint16_t *) &s_ram[0x44A] = m_textCols;           // Columns
  *(uint16_t *) &s_ram[0x44C] = m_textPageSize;       // Page size (bytes)
  *(uint16_t *) &s_ram[0x44E] = m_startAddress << 1;  // Start Address
  // Only for debug
  if ((m_startAddress << 1) != (m_activePage * m_textPageSize)) {
  	printf("mcga: Start address mismatch (page=%d, size=%d, addr=%d)\n",
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
  *(uint16_t *) &s_ram[0x463] = 0x03D4;          // CRTC Base Port
  s_ram[0x465] = m_modeControl;                // Mode Control Register
  s_ram[0x466] = m_colorSelect;                // Color Select Register
}

void MCGA::syncCursorPos()
{
  // Update cursor position in CRTC registers
  uint16_t pos = (uint16_t) m_cursorRow[m_activePage] * m_textCols +
                            m_cursorCol[m_activePage] + m_startAddress;

  m_crtc[MCGA_CRTC_CURSORPOS_HI] = (uint8_t) ((pos >> 8) & 0xFF);
  m_crtc[MCGA_CRTC_CURSORPOS_LO] = (uint8_t) ( pos       & 0xFF);

  // Update BDA
  s_ram[0x450 + 2*m_activePage  ] = m_cursorCol[m_activePage];
  s_ram[0x450 + 2*m_activePage+1] = m_cursorRow[m_activePage];
}

} // end of namespace
