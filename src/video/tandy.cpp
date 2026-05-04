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

#include "video/tandy.h"
#include "video/video_scanout.h"

#include "core/i8086.h"

#include <stdio.h>
#include <string.h>

#pragma GCC optimize ("O3")

#define CGA_TEXT_40x25_PAGE_SIZE  0x0800 // 2048
#define CGA_TEXT_80x25_PAGE_SIZE  0x1000 // 4096

#define CGA_VRAM_BANK_SIZE        0x2000

using fabgl::i8086; // CPU register access

namespace video {

Tandy::Tandy() :
  m_textAttr(0x07), // light gray on black
  m_graphicsColor(0x0F) // white
{
  m_vram = nullptr;
}

Tandy::~Tandy()
{
  m_video->stop();

  if (m_vram) {
    heap_caps_free((void *) m_vram);
    m_vram = nullptr;
  }
}

void Tandy::init(uint8_t *ram, VideoScanout *video)
{
  // External resources
  s_ram = ram;
  m_video = video;

  // Allocate video memory
  if (!m_vram) {
    printf("tandy: Allocating video memory (%d KB)\n", TGA_VRAM_SIZE / 1024);
    // Allocate video memory in DRAM (with DMA)
    m_vram = (uint8_t *) heap_caps_malloc(TGA_VRAM_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!m_vram) {
      printf("tandy: Not enough DRAM!!\n");
    }
  }

  m_video->setSource(this);

  reset();
}

void Tandy::reset()
{
  // Identify machine as Tandy 1000
  s_ram[0xF0000] = 'T';
  s_ram[0xF0001] = 'A';
  s_ram[0xF0002] = 'N';
  s_ram[0xF0003] = 'D';
  s_ram[0xF0004] = 'Y';
  s_ram[0xFFFFE] = 0xFD;

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

void Tandy::resetRegisters()
{
  // Clear CRTC Registers
  memset(m_crtc, 0, sizeof(m_crtc));
  m_crtcIndex = 0;

  // Default mode control and color set
  m_modeControl = TGA_MC_ENABLED |
                  TGA_MC_TEXT80COLS |
                  TGA_MC_BIT7BLINK;
  m_colorSelect = TGA_DEFAULT_COLORSELECT;

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

  m_crtc[TGA_CRTC_CURSORSTART] = m_cursorStart | (m_cursorDisable ? 0x20 : 0x00);
  m_crtc[TGA_CRTC_CURSOREND]   = m_cursorEnd;

  m_activePage = 0;

  // Cursor Position
  for (int i = 0; i < 8; i++) {
    m_cursorRow[i] = 0;
    m_cursorCol[i] = 0;
  }

  m_graphicsColor = 0x0F; // white, BIOS default
}

// --- INT 10h ---

void Tandy::handleInt10h()
{
  const uint8_t AH = i8086::AH();

  switch (AH) {

    // Set Video Mode
    case 0x00:
    {
      uint8_t mode = i8086::AL();

      // MDA Text Mode 80x25 (mono) - monochrome ignored by CGA
      if (mode == MDA_MODE_TEXT_80x25_MONO) {
        printf("tandy: Ignoring MDA text video mode (0x%02x)\n", mode);
        mode = CGA_MODE_TEXT_80x25_16COLORS;
      }

      if (mode == m_currentMode) {
        clearScreen();
        break; // Nothing to do
      }

      switch (mode) {

        // Text Mode 40x25
        case CGA_MODE_TEXT_40x25_16COLORS:
        case CGA_MODE_TEXT_40x25_16COLORS_ALT:
          m_modeControl = TGA_MC_ENABLED |
                          TGA_MC_BIT7BLINK;
          break;

        // Text Mode 80x25
        case CGA_MODE_TEXT_80x25_16COLORS:
        case CGA_MODE_TEXT_80x25_16COLORS_ALT:
        //case MDA_MODE_TEXT_80x25_MONO:
          m_modeControl = TGA_MC_ENABLED |
                          TGA_MC_TEXT80COLS |
                          TGA_MC_BIT7BLINK;
          break;

         // Graphics Mode 320x200
        case CGA_MODE_GFX_320x200_4COLORS:
        case CGA_MODE_GFX_320x200_4COLORS_ALT:
          m_modeControl = TGA_MC_ENABLED |
                          TGA_MC_GRAPHICS |
                          TGA_MC_BIT7BLINK;
          break;

        // Graphics Mode 640x200
        case CGA_MODE_GFX_640x200_2COLORS:
          m_modeControl = TGA_MC_ENABLED |
                          TGA_MC_GRAPHICS |
                          TGA_MC_HIGHRES |
                          TGA_MC_BIT7BLINK;
          break;

        case TGA_MODE_GFX_160x200_16COLORS:
        case TGA_MODE_GFX_320x200_16COLORS:
          m_modeControl = TGA_MC_ENABLED |
                          TGA_MC_TEXT80COLS |
                          TGA_MC_BIT7BLINK;
          m_graphicsColor = 0x0F; // white, BIOS default
          break;

        case TGA_MODE_GFX_640x200_4COLORS:
          m_modeControl = TGA_MC_ENABLED |
                          TGA_MC_GRAPHICS |
                          TGA_MC_HIGHRES |
                          TGA_MC_BIT7BLINK;
          m_graphicsColor = 0x0F; // white, BIOS default
          break;

        default:
          printf("tandy: Warning! Unexpected video mode (0x%02x)\n", mode);
          return;
      }
      m_colorSelect = TGA_DEFAULT_COLORSELECT;

      // Reset BIOS video state
      m_activePage = 0;
      m_startAddress = 0;
      for (int p = 0; p < 8; p++) {
        m_cursorRow[p] = 0;
        m_cursorCol[p] = 0;
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
      m_crtc[TGA_CRTC_CURSORSTART] = start;
      m_crtc[TGA_CRTC_CURSOREND]   = end;

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
        printf("tandy: Start address = 0x%04x (active page %d)\n", m_startAddress, m_activePage);
      }

      const uint8_t addr_hi = (uint8_t) ((m_startAddress >> 8) & 0xFF);
      const uint8_t addr_lo = (uint8_t) ( m_startAddress       & 0xFF);

      // Update BDA
      s_ram[0x44E] = addr_hi;
      s_ram[0x44F] = addr_lo;
      s_ram[0x462] = page;

      // Update CRTC Start Address
      m_crtc[TGA_CRTC_STARTADDR_HI] = addr_hi;
      m_crtc[TGA_CRTC_STARTADDR_LO] = addr_lo;

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

      printf("tandy: int 10h (AH=0x0B) Set palette %d %d\n", BH, BL);
      if (BH == 0x00) { // Set Background and Border Color (and Palette Intensity)
        // Clear current color (bits 0-3) and intensity (bit 4)
        m_colorSelect &= ~(TGA_CS_COLOR_MASK | TGA_CS_HIGHINTENSITY);

        // bits 0-3 : Set background / border color
        m_colorSelect |= (BL & 0x0F);

        // bit 4 : Set palette intensity (affects colors 1, 2, and 3)
        if (BL & 0x10) {
          m_colorSelect |= TGA_CS_HIGHINTENSITY;
        }
      } else if (BH == 0x01) { // Select Palette Set
        // BL bit 0 selects the palette:
        //   0 = Palette 0 (Green/Red/Brown)
        //   1 = Palette 1 (Cyan/Magenta/White)
        if (BL & 0x01) {
          m_colorSelect |= TGA_CS_PALETTESEL;
        } else {
          m_colorSelect &= ~TGA_CS_PALETTESEL;
        }
      }
      // Apply the new palette settings
      //TODO m_video->updateLUT();
      break;
    }
    
    // Write Pixel (BIOS AT / Extended)
    case 0x0C:
      if (isGraphicsMode()) {
        const uint8_t color = i8086::AL() & 0x0F;
        const uint32_t x = i8086::CX();
        const uint32_t y = i8086::DX();

        // Update the current graphics color
        m_graphicsColor = color;

        switch (m_currentMode) {

          case TGA_MODE_GFX_160x200_16COLORS:
            writePixel160x200x16(x, y, color);
            break;

          case TGA_MODE_GFX_320x200_16COLORS:
            writePixel320x200x16(x, y, color);
            break;

          case TGA_MODE_GFX_640x200_4COLORS:
            writePixel640x200x4(x, y, color);
            break;

          default:
            printf("tandy: Unhandled int 10h (AH=0x0C) (Write Pixel) mode=%d\n", m_currentMode);
            break;
        }
        printf("tandy: Write pixel (%d,%d) c=%d\n", x, y, color);
      }
      break;

    // Write Dot (BIOS PC/XT Original, 1981)
    case 0x0D:
      if (isGraphicsMode()) {
        const uint32_t x = i8086::CX();
        const uint32_t y = i8086::DX();
        uint8_t color = 0;

        switch (m_currentMode) {

          case TGA_MODE_GFX_160x200_16COLORS:
            color = readPixel160x200x16(x, y);
            break;

          case TGA_MODE_GFX_320x200_16COLORS:
            color = readPixel320x200x16(x, y);
            break;

          case TGA_MODE_GFX_640x200_4COLORS:
            color = readPixel640x200x4(x, y);
            break;

          default:
            printf("tandy: Unhandled int 10h (AH=0x0D) (Read Pixel) mode=%d\n", m_currentMode);
            break;
        }

        i8086::setAL(color);
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
      } else {
        if (color != 0) {
          // Optional but recommended for compatibility
          printf("color = %d\n", color);
          m_graphicsColor = color;
          // Note: Sierra and LucasArts used BIOS INT 10h AH=0Eh (Teletype) in Tandy graphics modes
          // to implicitly select the current drawing color, which then affected subsequent VRAM writes.
        }
      }
      break;
    }

    // Get Video Mode
    case 0x0F:
      i8086::setAL(m_currentMode);
      i8086::setAH(m_textCols); // Screen width in characters
      i8086::setBH(m_activePage);
      break;

    // EGA/VGA services - not supported on CGA
    case 0x10: // Palette Functions
    case 0x11: // Character Generator Functions
      i8086::setFlagCF(true);
      break;

    // Alternate Select / Video System Configuration
    case 0x12:
    {
      const uint8_t sub = i8086::BL();

#if 0
      // Only subfunction BL=10h is relevant for Tandy detection
      if (sub == 0x10) {
        // Return a Tandy-specific mode identifier in AL
        if (m_currentMode == TGA_MODE_GFX_160x200_16COLORS) {
          i8086::setAL(0x0A); // Tandy 160x200x16
        } else if (m_currentMode == TGA_MODE_GFX_320x200_16COLORS) {
          i8086::setAL(0x09); // Tandy 320x200x16
        } else if (m_currentMode == CGA_MODE_GFX_640x200_2COLORS) {
          i8086::setAL(0x0B); // 640x200 (used to distinguish from CGA)
        } else {
          i8086::setAL(0x12); // Group echo (no special graphics mode)
        }

        // Do NOT claim EGA or VGA capabilities
        i8086::setBH(0x00); // Color display
        i8086::setBL(0x00); // No EGA memory
        i8086::setCH(0x00); // Feature bits
        i8086::setCL(0x00); // Switch settings

        // Carry flag cleared: function supported
        i8086::setFlagCF(false);
      } else {
        // Other subfunctions: indicate group exists but subfunction unsupported
        i8086::setAL(0x12);
        i8086::setFlagCF(true);
      }
#else
      // Only subfunction BL=10h is relevant for Tandy detection
      if (sub == 0x10) {
        i8086::setBH(0x01); // Display Combination Code = Tandy 1000 / PCjr
        i8086::setBL(0x00); // No EGA memory
        i8086::setCH(0x00); // Feature bits
        i8086::setCL(0x00); // Switch settings

        i8086::setFlagCF(false);
      } else {
        // Other subfunctions: indicate group exists but subfunction unsupported
        i8086::setAL(0x12);
        i8086::setFlagCF(true);
      }
      printf("tandy: int 10h (AH=0x12) VSC sub=0x%02x\n", sub);
#endif
      break;
    }

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
#if 1
      i8086::setFlagCF(true); // Function not supported
      printf("tandy: int 10h (AH=0x1A) not supported\n");
#else
      i8086::setAL(0x1A); // Function identifier echo
      i8086::setBL(0x02); // Color graphics adapter
      i8086::setBH(0x00); // Reserved / secondary display (unused)
      i8086::setCH(0x00); // Reserved feature bits
      i8086::setCL(0x00); // Reserved switch settings
      i8086::setFlagCF(false);
      printf("tandy: int 10h (AH=0x1A)\n");
#endif
      break;

    default:
      printf("tandy: Unhandled int 10h (AH=0x%02x)\n", AH);
      break;
  }
}

// --- I/O Ports ---

uint8_t Tandy::readPort(uint16_t port)
{
  switch (port) {

    // CRTC Index
    case TGA_PORT_CRTCIDX:
      return m_crtcIndex;

    // CRTC Data
    case TGA_PORT_CRTCDATA:
      return m_crtc[m_crtcIndex];

    // Mode Control Register
    case TGA_PORT_MODECTRL:
      return m_modeControl;

    // Color Select Register
    case TGA_PORT_COLORSEL:
      return m_colorSelect;

    // Status Register
    // real vertical sync is too fast for our slowly emulated 8086, so
    // here it is just a fake, just to allow programs that check it to keep going anyway.
    case TGA_PORT_STATUS:
      m_VSyncQuery++;
      return (m_VSyncQuery & 0x7) != 0 ? 0x09 : 0x00; // "not VSync" (0x00) every 7 queries
      //TODO
      // Sincronizar con el render (scanout) real, algo más fino:
      // Bit 3 activo durante el VBlank real del VideoScanout
      // Bit 3 inactivo el resto del frame

    default:
      printf("tandy: Unhandled read (0x%04x)\n", port);
      return 0xFF;
  }
}

void Tandy::writePort(uint16_t port, uint8_t value)
{
  switch (port) {

    case 0x03C0 ... 0x03CF:
      // Probably it was detected an EGA video card
      printf("tandy: Unhandled write (0x%04x=0x%02x) EGA?\n", port, value);
      break;

    // CRTC Index
    case TGA_PORT_CRTCIDX:
      m_crtcIndex = value & 0x1F;
      break;

    // CRTC Data
    case TGA_PORT_CRTCDATA:
      m_crtc[m_crtcIndex] = value;
      switch (m_crtcIndex) {

        // Cursor Start
        case TGA_CRTC_CURSORSTART:
          // bits 0..4 : Cursor start scanline
          // bit  5    : Cursor disable
          m_cursorStart = value & 0x1F;
          m_cursorDisable = (value & 0x20) != 0;
          m_dirty = true;
          break;

        // Cursor End
        case TGA_CRTC_CURSOREND:
          // bits 0..4 : Cursor end scanline
          m_cursorEnd = value & 0x1F;
          m_dirty = true;
          break;

        // Start Address High and Low
        case TGA_CRTC_STARTADDR_HI:
        case TGA_CRTC_STARTADDR_LO:
        {
          const uint16_t addr_hi = (uint16_t) m_crtc[TGA_CRTC_STARTADDR_HI] << 8;
          const uint16_t addr_lo = (uint16_t) m_crtc[TGA_CRTC_STARTADDR_LO];
          const uint16_t oldAddr = m_startAddress;
          m_startAddress = addr_hi | addr_lo;
          if (m_startAddress != oldAddr) {
            printf("tandy: Start address = 0x%04x\n", m_startAddress);
          }
          m_dirty = true;
          break;
        }

        // Cursor Position High and Low
        case TGA_CRTC_CURSORPOS_HI:
        case TGA_CRTC_CURSORPOS_LO:
        {
          const uint16_t pos_hi = (uint16_t) m_crtc[TGA_CRTC_CURSORPOS_HI] << 8;
          const uint16_t pos_lo = (uint16_t) m_crtc[TGA_CRTC_CURSORPOS_LO];
          // Note that (pos_hi | pos_lo) is the absolute cursor address (in chars)
          const uint16_t cursorPos = (pos_hi | pos_lo) - m_startAddress;

          m_cursorRow[m_activePage] = (uint8_t) (cursorPos / m_textCols);
          m_cursorCol[m_activePage] = (uint8_t) (cursorPos % m_textCols);
          m_dirty = true;
          break;
        }

        default:
          printf("tandy: Unhandled crtc[0x%02x]=0x%02x\n", m_crtcIndex, value);
          break;
      }
      break;

    // CGA Mode Control Register
    case TGA_PORT_MODECTRL:
    {
      uint8_t mode;

      // Mode Control register always exists on Tandy
      m_modeControl = value;

      printf("tandy: Mode control register = 0x%02x\n", value);
      // In Tandy extended graphics modes (16-color modes),
      // writes to the CGA Mode Control register MUST NOT
      // change the active video mode.
      //
      // Real Tandy hardware ignores mode-switch semantics
      // while a Tandy extended mode is active.
      if ((m_currentMode == TGA_MODE_GFX_160x200_16COLORS) ||
          (m_currentMode == TGA_MODE_GFX_320x200_16COLORS) ||
          (m_currentMode == TGA_MODE_GFX_640x200_4COLORS)) {
        // Do not change mode, only update internal flags
        m_dirty = true;
        break;
      }

      // CGA-compatible behavior (normal operation)
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

    // CGA Color Select Register
    case TGA_PORT_COLORSEL:
      printf("tandy: Color select register = 0x%02x\n", value);
      m_colorSelect = value;
      m_video->updateLUT();
      break;

    case 0x03DB: // Clear Light Pen Latch
    case 0x03DC: // Preset Light Pen Latch
      break; // Nothing to do

    // Video Descriptions  D0 D7 D6
    // 8bpp   1 - 16K       0  0  0
    // 8bpp   2 -  8K       0  0  1
    // 4bpp   2 - 16K       0  1  0
    // 4bpp   4 -  8K       0  1  1
    // 2bpp   1 - 32K       1  0  0
    // 2bpp   2 - 32K       1  0  1
    // Bits per pixel (bpp) / # of banks / bank size
    // D0    = bit 0 in register 0x3DD
    // D6,D7 = bits 6,7 in register 0x3DF

    // Extended RAM Page Register - CPU Relative
    // bit 7 6 5 4 3 2 1 0
    //     | | | | | | | +- [0] Extended Addressing Modes
    //     | | | | | +-+--- [1,2] Reserved
    //     | | | +-+------- [3,4] CRT Video Page Address
    //     | +-+----------- [5,6] CPU Page Address
    //     +--------------- [7] Select 64K Or 256K RAM
    case TGA_PORT_EXTRAMPAGE:
      m_extMemPage = value;
      printf("tandy: Extended RAM page = 0x%02x (0x3DD)\n", value);
      break;

    // Write Video Array Data
    case TGA_PORT_ARRAYDATA:
      m_arrayData = value;
      printf("tandy: Video array data = 0x%02x (0x3DE)\n", value);
      break;

    // CRT Processor Page Register - Video Memory Relative
    // bit 7 6 5 4 3 2 1 0
    //     | | | | | +-+-+- [0-2] CRT Page
    //     | | +-+-+------- [3-5] Processor Page
    //     +-+------------- [6,7] Video Address Mode
    case TGA_PORT_CRTCPUPAGE:
      m_crtPage = value & 0x07;
      m_cpuPage = (value >> 3) & 0x07;
      // Address mode (0 = PCjr compatible, 1 = Tandy 1000 native)
      // Usually, 1 allows the full 32KB window access at 0xB8000
      m_addressMode = (value >> 6) & 0x03;
      printf("tandy: Memory page = 0x%02x (0x3DF)\n", value);
      break;

    default:
      printf("tandy: Unhandled write (0x%04x=0x%02x)\n", port, value);
      break;
  }
}

// --- Video Memory ---

uint8_t Tandy::readMem8(uint32_t physAddr)
{
#if 0
  if ((physAddr < TGA_VRAM_BASE) || (physAddr > TGA_VRAM_LIMIT)) {
    return 0xFF;
  }
#endif

  // Calculate the relative offset within the 32KB window (0x0000 to 0x7FFF)
  const uint32_t window = physAddr - TGA_VRAM_BASE;

  // Apply page mapping, m_cpuPage (from I/O port 0x3DF) selects which
  // 16KB bank of the 128KB VRAM is visible at the start of the window
  const uint32_t offset = window + (m_cpuPage * 16384);

  // Return the byte from VRAM
  return m_vram[offset % TGA_VRAM_SIZE];
}

uint16_t Tandy::readMem16(uint32_t physAddr)
{
  const uint16_t word_lo = (uint16_t) readMem8(physAddr);
  const uint16_t word_hi = (uint16_t) readMem8(physAddr + 1) << 8;
  return word_hi | word_lo;
}

void Tandy::writeMem8(uint32_t physAddr, uint8_t value)
{
#if 0
  // Check if the physical address falls within the TGA video window
  if ((physAddr < TGA_VRAM_BASE) || (physAddr > TGA_VRAM_LIMIT)) {
    return;
  }
#endif

  // Calculate the relative offset within the 32KB window
  const uint32_t window = physAddr - TGA_VRAM_BASE;

  // Map the window offset to the actual location in the 128KB system VRAM
  const uint32_t offset = window + (m_cpuPage * 16384);

  // Write value to the emulated VRAM buffer
  m_vram[offset % TGA_VRAM_SIZE] = value;
  m_dirty = true;
}

void Tandy::writeMem16(uint32_t physAddr, uint16_t value)
{
  writeMem8(physAddr,     (uint8_t) value & 0xFF);
  writeMem8(physAddr + 1, (uint8_t) (value >> 8) & 0xFF);
}

// --- Helpers ---

void Tandy::setMode(uint8_t mode)
{
  m_currentMode = mode;
  switch(m_currentMode) {

    case CGA_MODE_TEXT_40x25_16COLORS:
    case CGA_MODE_TEXT_40x25_16COLORS_ALT:
      m_textRows = 25;
      m_textCols = 40;
      m_textPageSize = CGA_TEXT_40x25_PAGE_SIZE;
      break;

    case CGA_MODE_TEXT_80x25_16COLORS:
    case CGA_MODE_TEXT_80x25_16COLORS_ALT:
    case MDA_MODE_TEXT_80x25_MONO:
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = CGA_TEXT_80x25_PAGE_SIZE;
      break;

    // In graphic modes we also update the number of text rows
    // and cols to answer INT 10h AH=0x0F
    case CGA_MODE_GFX_320x200_4COLORS:
    case CGA_MODE_GFX_320x200_4COLORS_ALT:
      m_textRows = 25;
      m_textCols = 40;
      m_textPageSize = CGA_TEXT_40x25_PAGE_SIZE;
      break;

    case CGA_MODE_GFX_640x200_2COLORS:
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = CGA_TEXT_80x25_PAGE_SIZE;
      break;

    case TGA_MODE_GFX_160x200_16COLORS:
    case TGA_MODE_GFX_320x200_16COLORS:
      m_textRows = 25;
      m_textCols = 40;
      m_textPageSize = CGA_TEXT_40x25_PAGE_SIZE;
      break;

    case TGA_MODE_GFX_640x200_4COLORS:
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = CGA_TEXT_80x25_PAGE_SIZE;
      break;

    default:
      break;
  }

  m_video->setMode(m_currentMode);
}

void Tandy::writePixel320x200x4(uint32_t x, uint32_t y, uint8_t value, bool xored)
{
  constexpr uint32_t rowLenBytes = 320 / 4;

  uint32_t offset = (y >> 1) * rowLenBytes   // row
                  + (x >> 2)                 // col
                  + ((y & 1) ? CGA_VRAM_BANK_SIZE : 0);  // bank

  uint32_t shift = 6 - (x & 3) * 2;
  uint8_t mask = (0x3 << shift);

  uint8_t &cell = m_vram[offset];

  if (xored) {
    cell ^= (value << shift);
  } else {
    cell = (cell & ~mask) | (value << shift);
  }
  m_dirty = true;
}

void Tandy::writePixel640x200x2(uint32_t x, uint32_t y, bool on)
{
  constexpr uint32_t rowLenBytes = 640 / 8;

  uint32_t offset = (y >> 1) * rowLenBytes   // row
                  + (x >> 3)                 // col
                  + ((y & 1) ? CGA_VRAM_BANK_SIZE : 0);  // bank

  uint8_t bit = 7 - (x & 7);
  uint8_t mask = (1 << bit);

  uint8_t &cell = m_vram[offset];

  if (on) {
    cell |= mask;
  } else {
    cell &= ~mask;
  }
  m_dirty = true;
}

void Tandy::writePixel160x200x16(uint32_t x, uint32_t y, uint8_t color)
{
  // One byte per pixel
  constexpr uint32_t bytesLine = 160;

  // Tandy interleaving
  const uint32_t bank = y & 3;
  const uint32_t row  = y >> 2;

  uint8_t *base = m_vram + (bank << 13) + (row * bytesLine);

  // Direct byte write
  base[x] = color & 0x0F;

  m_dirty = true;
}

void Tandy::writePixel320x200x16(uint32_t x, uint32_t y, uint8_t color)
{
  // Each byte contains two pixels
  constexpr uint32_t bytesLine = 160;

  // Tandy interleaving
  const uint32_t bank = y & 3;
  const uint32_t row  = y >> 2;

  uint8_t *base = m_vram + (bank << 13) + (row * bytesLine);

  // Byte index and nibble selection
  const uint32_t byteIndex = x >> 1;
  const bool highNibble = (x & 1) == 0;

  uint8_t &cell = base[byteIndex];

  if (highNibble) {
    // Pixel in high nibble (bits 7..4)
    cell = (cell & 0x0F) | ((color & 0x0F) << 4);
  } else {
    // Pixel in low nibble (bits 3..0)
    cell = (cell & 0xF0) | (color & 0x0F);
  }
  m_dirty = true;
}

void Tandy::writePixel640x200x4(uint32_t x, uint32_t y, uint8_t color)
{
  // 640 pixels / 4 pixels per byte = 160 bytes per row
  constexpr uint32_t rowBytes = 160;
  constexpr uint32_t bankSize = CGA_VRAM_BANK_SIZE;

  const uint32_t offset = (y >> 1) * rowBytes       // row
                        + (x >> 2)                  // byte within row
                        + ((y & 1) ? bankSize : 0); // odd/even bank

  const uint8_t shift = 6 - ((x & 3) << 1);
  const uint8_t mask  = 0x03 << shift;

  uint8_t &cell = m_vram[offset];
  cell = (cell & ~mask) | ((color & 0x03) << shift);

  m_dirty = true;
}

uint8_t Tandy::readPixel160x200x16(uint32_t x, uint32_t y) const
{
  constexpr uint32_t bytesLine = 160; // 1 byte per pixel

  // Tandy 4-bank interleaving
  const uint32_t bank = y & 3;
  const uint32_t row  = y >> 2;

  const uint8_t *base = m_vram + (bank << 13) + (row * bytesLine);

  return base[x] & 0x0F;
}

uint8_t Tandy::readPixel320x200x16(uint32_t x, uint32_t y) const
{
  constexpr uint32_t bytesLine = 160;

  // Tandy 4-bank interleaving
  const uint32_t bank = y & 3;
  const uint32_t row  = y >> 2;

  const uint8_t *base = m_vram + (bank << 13) + (row * bytesLine);

  const uint32_t byteIndex = x >> 1;
  const bool highNibble = (x & 1) == 0;

  const uint8_t cell = base[byteIndex];

  if (highNibble) {
    // High nibble (bits 7..4)
    return (cell >> 4) & 0x0F;
  } else {
    // Low nibble (bits 3..0)
    return cell & 0x0F;
  }
}

uint8_t Tandy::readPixel640x200x4(uint32_t x, uint32_t y) const
{
  constexpr uint32_t bytesLine = 160;
  constexpr uint32_t bankSize = CGA_VRAM_BANK_SIZE;

  const uint32_t offset = (y >> 1) * bytesLine
                        + (x >> 2)
                        + ((y & 1) ? bankSize : 0);

  const uint8_t shift = 6 - ((x & 3) << 1);
  return (m_vram[offset] >> shift) & 0x03;
}

void Tandy::writeCharAttr(uint8_t ch, uint8_t attr, uint16_t count, uint8_t page)
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

void Tandy::writeCharOnly(uint8_t ch, uint16_t count, uint8_t page)
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
void Tandy::scrollUpWindow(uint8_t lines, uint8_t attr,
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
void Tandy::scrollDownWindow(uint8_t lines, uint8_t attr,
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

void Tandy::ttyOutput(uint8_t ch, uint8_t page, uint8_t color)
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

void Tandy::writeString(uint8_t flags, uint8_t page, uint8_t attr,
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

void Tandy::newLine(uint8_t page)
{
  m_cursorCol[page] = 0;
  m_cursorRow[page]++;
  if (m_cursorRow[page] >= m_textRows) {
    // Scroll the page and clamp the cursor to the last row.
    scrollUp(page);
    m_cursorRow[page] = m_textRows - 1;
  }
}

void Tandy::scrollUp(uint8_t page)
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

void Tandy::clearScreen()
{
  // Clear video memory
  memset(m_vram, 0, TGA_VRAM_SIZE);

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

void Tandy::syncBDA()
{
  // s_ram[0x449] = 0x03;
  *(uint16_t *) &s_ram[0x44A] = m_textCols;      // Columns
  *(uint16_t *) &s_ram[0x44C] = m_textPageSize;  // Page size (bytes)
  *(uint16_t *) &s_ram[0x44E] = m_startAddress << 1;  // Start Address
  // Only for debug
  if ((m_startAddress << 1) != (m_activePage * m_textPageSize)) {
  	printf("tandy: Start address mismatch (page=%d, size=%d, addr=%d)\n",
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

void Tandy::syncCursorPos()
{
  // Update cursor position in CRTC registers
  uint16_t pos = (uint16_t) m_cursorRow[m_activePage] * m_textCols +
                            m_cursorCol[m_activePage] + m_startAddress;

  m_crtc[TGA_CRTC_CURSORPOS_HI] = (uint8_t) ((pos >> 8) & 0xFF);
  m_crtc[TGA_CRTC_CURSORPOS_LO] = (uint8_t) ( pos       & 0xFF);

  // Update BDA
  s_ram[0x450 + 2*m_activePage  ] = m_cursorCol[m_activePage];
  s_ram[0x450 + 2*m_activePage+1] = m_cursorRow[m_activePage];
}

} // end of namespace
