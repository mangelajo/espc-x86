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

#pragma once

#include "video/cga_palette.h"

#include "video/video_adapter.h"
#include "video/scanout_context.h"

#include <stdint.h>
#include <stdlib.h>

// Video Memory
#define MCGA_VRAM_SIZE           65536

// Text mode video memory segment: B800h
#define MCGA_VRAM_TEXT_BASE    0xB8000
#define MCGA_VRAM_TEXT_LIMIT   0xBBFFF // 0xB8000 + 16 KB - 1

// Graphics mode video memory segment: A000h
#define MCGA_VRAM_GFX_BASE     0xA0000
#define MCGA_VRAM_GFX_LIMIT    0xAFFFF // 0xA0000 + 64 KB - 1

// IO Ports
#define MCGA_PORT_CRTCIDX       0x03D4
#define MCGA_PORT_CRTCDATA      0x03D5
#define MCGA_PORT_MODECTRL      0x03D8
#define MCGA_PORT_COLORSEL      0x03D9
#define MCGA_PORT_STATUS        0x03DA

// CRTC (Cathode Ray Tube Controller) Registers
#define MCGA_CRTC_CURSORSTART     0x0A
#define MCGA_CRTC_CURSOREND       0x0B
#define MCGA_CRTC_STARTADDR_HI    0x0C
#define MCGA_CRTC_STARTADDR_LO    0x0D
#define MCGA_CRTC_CURSORPOS_HI    0x0E
#define MCGA_CRTC_CURSORPOS_LO    0x0F

// Mode Control Register
#define MCGA_MC_TEXT80COLS        0x01
#define MCGA_MC_GRAPHICS          0x02
#define MCGA_MC_MONOCHROME        0x04
#define MCGA_MC_ENABLED           0x08
#define MCGA_MC_HIGHRES           0x10
#define MCGA_MC_BIT7BLINK         0x20

// Color Select Register

#define MCGA_CS_COLOR_MASK        0x0F
#define MCGA_CS_HIGHINTENSITY     0x10
#define MCGA_CS_PALETTESEL        0x20

#define MCGA_DEFAULT_COLORSELECT  0x30
// Default value in MCGA RGB (IBM 5153)
//#define MCGA_DEFAULT_COLORSELECT  0x00

using fabgl::RGB222;

namespace video {

// MCGA (VGA Mode 13h) full palette for LilyGo VGA32
// Hardware limit: 64 physical colors (2 bits per channel)
static RGB222 MCGA_palette[256];

class VideoScanout;

// MCGA video card emulation
class MCGA : public VideoAdapter,
             public ScanoutContext {

public:

   MCGA();
  ~MCGA();

  void init(uint8_t *ram, VideoScanout *video);
  void reset();

  // BIOS Video Interrupt (INT 10h)
  void handleInt10h();

  // I/O Ports
  uint8_t readPort(uint16_t port);
  void writePort(uint16_t port, uint8_t value);

  // Mapped Memory
  uint8_t  readMem8(uint32_t physAddr);
  uint16_t readMem16(uint32_t physAddr);
  void writeMem8(uint32_t physAddr, uint8_t value);
  void writeMem16(uint32_t physAddr, uint16_t value);

  // Scanout Context
  uint8_t *vram() override { return m_vram; }
  size_t vramSize() override { return MCGA_VRAM_SIZE; }

  uint32_t startAddress() override { return (uint32_t) m_startAddress; }
  uint16_t textPageSize() override { return m_textPageSize; }
  bool cursorEnabled() override { return !m_cursorDisable; }
  uint8_t cursorStart() override { return m_cursorStart; }
  uint8_t cursorEnd() override { return m_cursorEnd; }
  uint8_t activePage() override { return m_activePage; }
  uint8_t cursorRow(uint8_t page) override { return m_cursorRow[page]; }
  uint8_t cursorCol(uint8_t page) override { return m_cursorCol[page]; }
  RGB222 paletteMap(uint8_t index, uint8_t total) override {
    return MCGA_palette[index];
  }
  bool blinkEnabled() override { return isBit7Blinking(); }
  uint8_t colorPlaneEnable() override { return 1; }

  uint32_t renderStamp() { return m_stamp; }

private:

  // --- External ---
  uint8_t *s_ram; // Main Memory (needed to update BDA)

  VideoScanout *m_video; // Renderer

  // --- Internal ---
  uint8_t *m_vram; // Video Memory

  // CRTC Registers (Motorola 6845)
  uint8_t m_crtc[0x20];
  uint8_t m_crtcIndex;

  uint8_t m_dacPalette[256][3]; 

  uint8_t m_modeControl; // Mode Control Register
  uint8_t m_colorSelect; // Color Select Register

  uint16_t m_VSyncQuery;
  uint16_t m_startAddress; // in words

  bool m_cursorDisable;

  // Cursor Shape
  uint8_t m_cursorStart;
  uint8_t m_cursorEnd;

  uint8_t m_activePage;

  // Cursor Position
  uint8_t m_cursorRow[8];
  uint8_t m_cursorCol[8];

  uint16_t m_currentMode;

  // Text mode dimensions (40x25 and 80x25)
  uint8_t  m_textRows;
  uint8_t  m_textCols;
  uint16_t m_textPageSize; // in bytes

  // Default Text Attribute (foreground 0x0F / background 0xF0)
  const uint8_t m_textAttr;

  bool m_dirty;
  uint32_t m_stamp;

  void initPalette();
  void resetRegisters();

  void setMode(uint8_t mode);

  // Mode Control
  inline bool isText80Columns()  const { return (m_modeControl & MCGA_MC_TEXT80COLS) != 0; }
  inline bool isGraphicsMode()   const { return (m_modeControl & MCGA_MC_GRAPHICS) != 0; }
  inline bool isHighResolution() const { return (m_modeControl & MCGA_MC_HIGHRES) != 0; }
  inline bool isVideoEnabled()   const { return (m_modeControl & MCGA_MC_ENABLED) != 0; }
  inline bool isBit7Blinking()   const { return (m_modeControl & MCGA_MC_BIT7BLINK) != 0; }

  // Color Select
  inline uint8_t paletteIndex() const {
    const uint8_t paletteAltern = (m_colorSelect & MCGA_CS_PALETTESEL)    ? 2 : 0;
    const uint8_t highIntensity = (m_colorSelect & MCGA_CS_HIGHINTENSITY) ? 1 : 0;
    return paletteAltern | highIntensity;
  }
  // Text mode: Border Color (Overscan)
  // Graphics mode 320x200: Background Color (Color 0)
  // Graphics mode 640x200: Foreground Color (for active pixels, value 1).
  inline uint8_t colorSelect() const { return m_colorSelect & MCGA_CS_COLOR_MASK; }

  void writePixel320x200(uint32_t x, uint32_t y, uint8_t value);
  void writePixel640x200(uint32_t x, uint32_t y, bool on);

  void writeCharAttr(uint8_t ch, uint8_t attr, uint16_t count, uint8_t page);
  void writeCharOnly(uint8_t ch, uint16_t count, uint8_t page);

  void scrollUpWindow(uint8_t lines, uint8_t attr, uint8_t top, uint8_t left, uint8_t bottom, uint8_t right);
  void scrollDownWindow(uint8_t lines, uint8_t attr, uint8_t top, uint8_t left, uint8_t bottom, uint8_t right);

  // Writes a single character using teletype rules (advance, wrap, scroll)
  void ttyOutput(uint8_t ch, uint8_t page, uint8_t color);

  void writeString(uint8_t flags, uint8_t page, uint8_t attr,
                   uint8_t row, uint8_t col,
                   uint16_t ES, uint16_t BP, uint16_t len);

  void newLine(uint8_t page);

  // Scrolls the specified text page up by 1 line and clears the last line
  void scrollUp(uint8_t page);

  // Computes linear byte offset (within VRAM window) for a text cell
  inline uint32_t textCellOffset(uint8_t page, uint8_t row, uint8_t col) const {
    return (uint32_t) page * m_textPageSize + ((uint32_t) row * m_textCols + col) * 2;
  }

  void clearScreen();

  // Update BIOS Data Area (BDA)
  void syncBDA();

  // Syncs the cursor position with CRTC registers
  // and update BDA
  void syncCursorPos();
};

} // end of namespace
