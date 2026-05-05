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

#include "video/ega.h"
#include "video/ega_modes.h"
#include "video/ega_palette.h"
#include "video/cga_palette.h"
#include "video/video_scanout.h"

#include "core/i8086.h"
#include "bios.h"

#include <stdio.h>
#include <string.h>

#pragma GCC optimize ("O3")

using fabgl::i8086; // CPU register access

namespace video {

EGA::EGA()
  : m_textAttr(0x07) // light gray on black
{
#if EGA_NON_SEGMENTED_VRAM
  m_vram = nullptr;
#else
  for (int i = 0; i < 4; i++) {
    m_plane[i] = nullptr;
  }
#endif
}

EGA::~EGA()
{
  m_video->stop();

#if EGA_NON_SEGMENTED_VRAM
  if (m_vram) {
    heap_caps_free((void *) m_vram);
    m_vram = nullptr;
  }
#else
  for (int i = 0; i < 4; i++) {
    if (m_plane[i]) {
      heap_caps_free((void *) m_plane[i]);
      m_plane[i] = nullptr;
    }
  }
#endif
}

void EGA::init(uint8_t *ram, VideoScanout *video)
{
  // External resources
  s_ram = ram;
  m_video = video;

#if EGA_NON_SEGMENTED_VRAM
  // Allocate video memory
  if (!m_vram) {
    printf("ega: Allocating video memory (%d KB)\n", EGA_VRAM_SIZE / 1024);
    // Allocate video memory in DRAM (with DMA)
    m_vram = (uint8_t *) heap_caps_malloc(EGA_VRAM_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!m_vram) {
      printf("ega: Not enough DRAM!\n");
      printf("ega: Free internal DRAM = %lu KB\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
      printf("ega: Free DMA DRAM = %lu KB\n", heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024);
      printf("ega: Largest internal|DMA free block = %lu KB\n",
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024);
    }
    for (int i = 0; i < 4; i++) {
      m_plane[i] = (uint8_t *) m_vram + i * m_planeSize;
    }
  }
#else
  // Allocate video memory
  if (!m_plane[0]) {
    printf("ega: Allocating video memory (%d KB)\n", EGA_VRAM_SIZE / 1024);
    printf("ega: Planar video memory (%d KB/plane)\n", m_planeSize / 1024);
    for (int i = 0; i < 4; i++) { // for each plane
      // Allocate video memory in DRAM (with DMA)
      m_plane[i] = (uint8_t *) heap_caps_malloc(m_planeSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!m_plane[i]) {
        printf("ega: Not enough DRAM!\n");
        printf("ega: Free internal DRAM = %lu KB\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
        printf("ega: Free DMA DRAM = %lu KB\n", heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024);
        printf("ega: Largest internal|DMA free block = %lu KB\n",
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024);
      }
    }
  }
#endif // EGA_NON_SEGMENTED_VRAM

  installROM();
  initPalette();

  m_video->setSource(this);

  reset();
}

void EGA::reset()
{
  m_video->stop();

#if EGA_NON_SEGMENTED_VRAM
  // Clear video memory
  memset(m_vram, 0, EGA_VRAM_SIZE);
#else
  for (int i = 0; i < 4; i++) {
    memset(m_plane[i], 0, m_planeSize);
  }
#endif

  // Clear video card registers and state values
  resetRegisters();

  // Initialize video card registers and state values
  setMode(CGA_MODE_TEXT_80x25_16COLORS);

  // Update BIOS Data Area
  s_ram[0x449] = m_currentMode;
  syncBDA();
  //checkROM();

  m_video->run();
}

void EGA::initPalette()
{
  // Lookup table for 2-bit (0..3) to 8-bit (0x00,0x55,0xAA,0xFF) conversion.
  const uint8_t lut_2_to_8[4] = { 0x00, 0x55, 0xAA, 0xFF };

  // Build the 64-color EGA palette
  for (uint8_t index = 0; index < 64; index++) {
    // Extract L/H bits per channel according to EGA's canonical order:
    // [5]=R-L, [4]=G-L, [3]=B-L, [2]=R-H, [1]=G-H, [0]=B-H
    uint8_t rL = (index >> 5) & 1;
    uint8_t gL = (index >> 4) & 1;
    uint8_t bL = (index >> 3) & 1;
    uint8_t rH = (index >> 2) & 1;
    uint8_t gH = (index >> 1) & 1;
    uint8_t bH = (index >> 0) & 1;

    // Rebuild 2-bit channel values (0..3): value2 = L + 2*H
    uint8_t r2 = rL + (rH << 1);
    uint8_t g2 = gL + (gH << 1);
    uint8_t b2 = bL + (bH << 1);

    // Expand to 8-bit RGB using the EGA standard steps
    m_palette[index].r = lut_2_to_8[r2];
    m_palette[index].g = lut_2_to_8[g2];
    m_palette[index].b = lut_2_to_8[b2];
  }
}

void EGA::resetRegisters()
{
  // Clear Sequencer Registers
  memset(m_seq, 0, sizeof(m_seq));
  m_seqIndex = 0;

  // Clear Graphics Controller Registers
  memset(m_gc, 0, sizeof(m_gc));
  m_gcIndex = 0;

  // Clear CRT Controller Registers
  memset(m_crtc, 0, sizeof(m_crtc));
  m_crtcIndex = 0;

  // Clear Attribute Controller Registers
  memset(m_attr, 0, sizeof(m_attr));
  m_attrIndex = 0;
  m_attrFlipFlop = false;

  // Default EGA palette mapping
  for (int i = 0; i < 16; i++) {
    m_paletteMap[i] = EGA_paletteMap[i];
  }

  m_frameCounter = 0;

  m_cga_modeControl = 0x00;
  m_cga_colorSelect = 0x00;
}

// --- INT 10h ---

void EGA::handleInt10h()
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

      if (videoMode == MDA_MODE_TEXT_80x25_MONO) {
        printf("ega: Ignoring MDA text mode (0x%02x)\n", videoMode);
        videoMode = CGA_MODE_TEXT_80x25_16COLORS;
      }

      if (videoMode == m_currentMode) {
        if (cls)
          clearScreen();
        break; // Nothing to do
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
      m_crtc[EGA_CRTC_CURSORSTART] = start;
      m_crtc[EGA_CRTC_CURSOREND]   = end;

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
      printf("ega: Unhandled int 10h (AH=0x%02x)\n", AH); // Unsupported
      return;

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
      m_crtc[EGA_CRTC_STARTADDR_HI] = addr_hi;
      m_crtc[EGA_CRTC_STARTADDR_LO] = addr_lo;

      syncCursorPos();
      m_dirty = true;
      printf("ega: int 10h (AH=0x05) active_page=%d\n", page);
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
      if (m_textMode) {
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
      if (m_textMode) {
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
      i8086::setAL(m_plane[0][offset + 0]); // character
      i8086::setAH(m_plane[0][offset + 1]); // attribute
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
      if (m_textMode) {
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
      if (m_textMode) {
        writeCharOnly(ch, count, page);
      }
      break;
    }

    // Set Palette / Background (CGA/EGA/VGA)
    case 0x0B:
    {
      uint8_t BH = i8086::BH();
      uint8_t BL = i8086::BL();
      printf("ega: set palette %d %d\n", BH, BL);

      if (BH == 0x00) {         // set background/border (overscan)
        // Attribute Controller overscan register is commonly mirrored at index 0x11
        m_attr[0x11] = BL & 0x3F;  // 6-bit EGA entry if applicable
        m_dirty = true;
      } else if (BH == 0x01) {  // set palette (CGA-style palette select)
        // On EGA this is typically superseded by AH=10h palette functions
        // You may store BL for compatibility or just accept as no-op
        m_attr[0x12] = BL;         // optional stash (non-standard index)
        m_dirty = true;
      }
      // Other BH values are not defined for EGA
      // Apply the new palette settings
      //m_video->updateLUT();
      break;
    }

    // Write Graphics Pixel
    case 0x0C:
    {
      if (m_textMode)
        return;

      // (AL=color, BH=page, CX=X, DX=Y)  (CGA/EGA/VGA)
      // determine current logical resolution by BIOS mode
      int width = 0;
      int height = 0;

      if (m_currentMode == 0x0D)      { width = 320; height = 200; }
      else if (m_currentMode == 0x0E) { width = 640; height = 200; }
      else if (m_currentMode == 0x10) { width = 640; height = 350; }
      else return;

      uint8_t color = i8086::AL();
      // uint8_t BH = i8086::BH();  // page ignored in our EGA pipeline
      int x = (int) i8086::CX();
      int y = (int) i8086::DX();

      if (x < 0 || y < 0 || x >= width || y >= height)
        return;  // out of bounds: ignore

      // compute byte offset: base + y * lineOffset + x/8
      int lineOffset = crtcLineOffsetBytes();
      if (lineOffset <= 0)
        lineOffset = width / 8;

      size_t base = size_t(y) * size_t(lineOffset);
      size_t byteOffset = base + size_t(x >> 3);
      int bitpos = 7 - (x & 7);

      // write planar bits according to AL (low 4 bits)
      uint8_t idx4 = color & 0x0F;
      uint8_t mask = uint8_t(1 << bitpos);

      for (int p = 0; p < 4; p++) { // for each plane
        uint8_t *bytep = &m_plane[p][byteOffset];
        if ((idx4 >> p) & 1) {
          *bytep = *bytep | mask;
        } else {
          *bytep = *bytep & ~mask;
        }
      }

      m_dirty = true;
      break;
    }

    // Read Graphics Pixel
    case 0x0D:
    {
      if (m_textMode)
        return;

      // (BH=page, CX=X, DX=Y) -> AL=color  (CGA/EGA/VGA)
      int width = 0;
      int height = 0;

      if (m_currentMode == 0x0D)      { width = 320; height = 200; }
      else if (m_currentMode == 0x0E) { width = 640; height = 200; }
      else if (m_currentMode == 0x10) { width = 640; height = 350; }
      else return;

      // uint8_t BH = i8086::BH();  // page ignored
      int x = (int) i8086::CX();
      int y = (int) i8086::DX();

      if (x < 0 || y < 0 || x >= width || y >= height) {
        // return 0 (black) when out of bounds (common behavior)
        i8086::setAL(0x00);
        return;
      }

      int lineOffset = crtcLineOffsetBytes();
      if (lineOffset <= 0)
        lineOffset = width / 8;

      size_t base = size_t(y) * size_t(lineOffset);
      size_t byteOffset = base + size_t(x >> 3);
      int bitpos = 7 - (x & 7);
      uint8_t mask = (uint8_t) 1 << bitpos;

      uint8_t b0 = m_plane[0][byteOffset];
      uint8_t b1 = m_plane[1][byteOffset];
      uint8_t b2 = m_plane[2][byteOffset];
      uint8_t b3 = m_plane[3][byteOffset];

      uint8_t idx4 = 0;
      idx4 |= ((b0 & mask) ? 1 : 0) << 0;
      idx4 |= ((b1 & mask) ? 1 : 0) << 1;
      idx4 |= ((b2 & mask) ? 1 : 0) << 2;
      idx4 |= ((b3 & mask) ? 1 : 0) << 3;

      i8086::setAL(idx4);
      break;
    }

    // Teletype Output
    case 0x0E:
    {
      const uint8_t ch    = i8086::AL();
      const uint8_t page  = i8086::BH() & 0x07;
      const uint8_t color = i8086::BL(); // Optional foreground color

      // Only in text modes, in graphics mode ignore
      if (m_textMode) {
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

    // EGA Palette Functions
    case 0x10:
    {
      const uint8_t AL = i8086::AL();
      switch (AL) {
        case 0x00: // Set Individual Palette Register
        {
          uint8_t reg = i8086::BL() & 0x0F;  // 0..15
          uint8_t val = i8086::BH() & 0x3F;  // 0..63
          m_paletteMap[reg] = val;
          m_dirty = true;
          break;
        }

        case 0x01: // Set Overscan/Border
        {
          uint8_t val = i8086::BH() & 0x3F;
          // Attribute Controller overscan (commonly 0x11)
          m_attr[0x11] = val;
          m_dirty = true;
          break;
        }

        case 0x02: // Set All Palette Registers + border from ES:DX
        {
          const uint16_t ES = i8086::ES();
          const uint16_t DX = i8086::DX();
          const uint32_t hostAddr = ((uint32_t) ES << 4) + DX;
          const uint8_t *tbl = s_ram + hostAddr;
          for (int i = 0; i < 16; i++)
            m_paletteMap[i] = tbl[i] & 0x3F;
          m_attr[0x11] = tbl[16] & 0x3F;
          m_dirty = true;
          // Apply the new palette settings
          //m_video->updateLUT();
          break;
        }

        case 0x03: // Blink vs Bright Background (EGA)
        {
          uint8_t BL = i8086::BL();
          bool blink = (BL != 0x00);
          printf("ega: palette blink=%d\n", blink);
          m_dirty = true;
          break;
        }

        default:
          printf("ega: Unhandled int 10h (AH=%02x)\n", AH);
          break;
      }
      break;
    }

    // EGA Character Generator Functions
    case 0x11:
    {
      const uint8_t AL = i8086::AL();
      const uint16_t ES = i8086::ES();
      const uint16_t BP = i8086::BP();
      const uint16_t CX = i8086::CX();
      const uint8_t  BL = i8086::BL();

      // Helper: load an 8×8 font block into plane 2 (EGA behavior)
      auto loadFont8x8 = [&](int mapSelect) {
        // mapSelect = 0 -> set 1 -> offset 0x0000
        // mapSelect = 1 -> set 2 -> offset 0x2000
        uint32_t base = (mapSelect == 0 ? 0x0000 : 0x2000);

        // EGA font RAM is stored in PLANE 2.
        // We temporarily force MapMask = 0x04 (plane 2 only).
        uint8_t oldMask = m_mapMask;
        m_seq[EGA_SEQ_MAPMASK] = 0x04;
        m_mapMask = 0x04;

        // Disable odd/even to access raw bytes
        bool oldOE = m_oddEvenAddressing;
        m_oddEvenAddressing = false;

        uint32_t hostAddr = (ES << 4) + BP;

        for (int ch = 0; ch < CX; ch++) {
          uint32_t dst = base + ch * 8;
          for (int row = 0; row < 8; row++) {
            uint8_t b = s_ram[hostAddr + ch * 8 + row];
            m_plane[2][dst + row] = b;
          }
        }

        // Restore state
        m_seq[EGA_SEQ_MAPMASK] = oldMask;
        m_mapMask = oldMask;
        m_oddEvenAddressing = oldOE;
        printf("ega: int 10h (EGA Character Generator Function)\n");
      };

      switch(AL) {
        // Set primary character map (ROM A/B)
        case 0x00:
          // SEQ[03] bits 0-1 = primary map select
          m_seq[EGA_SEQ_CHARMAPSEL] = (m_seq[EGA_SEQ_CHARMAPSEL] & ~0x03) | (BL & 0x01);
          break;

        // Set secondary character map
        case 0x01:
          // SEQ[03] bits 2-3 = secondary map select
          m_seq[EGA_SEQ_CHARMAPSEL] = (m_seq[EGA_SEQ_CHARMAPSEL] & ~0x0C) | ((BL & 0x01) << 2);
          break;

        // Load 8x8 font (set 1 -> map A)
        case 0x03:
          loadFont8x8(0); // map A
          // Set char height = 8
          m_crtc[0x09] = 7;
          s_ram[0x485] = 8;
          break;

        // Load 8x8 font (set 2 -> map B)
        case 0x04:
          loadFont8x8(1); // map B
          // Set char height = 8
          m_crtc[0x09] = 7;
          s_ram[0x485] = 8;
          break;

        // Select 8x14 font (ROM EGA native)
        case 0x10:
          // CRTC max scan line = 13 -> 14 lines height
          m_crtc[0x09] = 13;
          s_ram[0x485] = 14;

          // SEQ[03] = 00h (ROM 8×14 uses map A/B defaults)
          m_seq[EGA_SEQ_CHARMAPSEL] &= ~0x0F;
          break;

        // Select 8×8 font (ROM)
        case 0x11:
          m_crtc[0x09] = 7; // 8 scanlines
          s_ram[0x485] = 8; // update BDA
          break;

        // Return info about current font
        case 0x20:
        {
          const uint8_t height = (m_crtc[0x09] & 0x1F) + 1;
          i8086::setDH(height);

          // ES:BP = pointer to BIOS font table – we do not emulate ROM fonts
          // DOS rarely uses this, so safest is return a benign pointer:
          //i8086::setES(0xF000);
          //i8086::setBP(0xFA6E); // Vector usually used by BIOS font tables
          break;
        }

        default:
          printf("ega: Unhandled int 10h (AH=0x11, AL=0x%02x)\n", AL);
          break;
      }
      break;
    }

    // EGA/VGA Alternate Select / Video Subsystem Configuration
    case 0x12:
    {
      uint8_t BL = i8086::BL();
printf("INT 10h AH=0x12 BL=%d <----------------------------------------------------------\n", BL);
      if (BL == 0x10) { // Return EGA configuration info
        // BH = 0 color, 1 mono (report color)
        i8086::setBH(0x00);

        // BL = EGA memory size code: 0=64K, 1=128K, 2=192K, 3=256K
        uint8_t memCode = 0;  // default 64K
        if (EGA_VRAM_SIZE >= 256 * 1024)      memCode = 3;
        else if (EGA_VRAM_SIZE >= 192 * 1024) memCode = 2;
        else if (EGA_VRAM_SIZE >= 128 * 1024) memCode = 1;

        i8086::setBL(memCode);

        // CH = feature bits, CL = switch settings (return 0 if you don't emulate DIP/features)
        i8086::setCH(0x00);
        i8086::setCL(0x00);

        // Many BIOSes return AL=12h as "group echo"
        i8086::setAL(0x12);

        break;
      }

      // other BL subfunctions (20h, 30h..36h) are VGA/PS2 specific;
      // EGA real no soporta 30h en general, así que no afirmes capacidades que no emulas
      // return with AL=12h to indicate the group exists but subfunction may be no-op
      i8086::setAL(0x12);
      return;
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

// Enabling this service will cause the video card to be detected as a VGA card 
#if 0
    // Video Combination Code (VCC)
    case 0x1A:
    {
      const uint8_t sub = i8086::AL();

      // AL=00h -> Get video combination code
      // AL=01h -> Set video combination code (not typically supported on EGA; no-op)
      if (sub == 0x00) {
        // Function supported
        i8086::setAL(0x1A);

        // Map current BIOS mode to a Video Combination Code in BL
        // (BH = inactive display -> 00h)
        uint8_t vcc = 0x04; // default: EGA color

        // Treat your CGA-compatible text/graphics modes as CGA color
        switch (m_currentMode) {
          // Text (CGA-compatible)
          case CGA_MODE_TEXT_40x25_16COLORS:
          case CGA_MODE_TEXT_40x25_16COLORS_ALT:
          case CGA_MODE_TEXT_80x25_16COLORS:
          case CGA_MODE_TEXT_80x25_16COLORS_ALT:
          // CGA graphics (320x200x4, 640x200x2)
          case CGA_MODE_GFX_320x200_4COLORS:
          case CGA_MODE_GFX_320x200_4COLORS_ALT:
          case CGA_MODE_GFX_640x200_2COLORS:
            vcc = 0x02; // CGA, color display
            break;

          // Monochrome text (if you ever use it)
          case MDA_MODE_TEXT_80x25_MONO:
            vcc = 0x05; // EGA, monochrome display
            break;

          // Native EGA graphics
          case EGA_MODE_GFX_320x200_16COLORS:
          case EGA_MODE_GFX_640x200_16COLORS:
          case MDA_MODE_GFX_640x350_MONO:
          case EGA_MODE_GFX_640x350_16COLORS:
            vcc = 0x04; // EGA, color display (mono mode could also be 0x05)
            break;

          default:
            // Keep 0x04 (EGA color) as a safe default
            break;
        }

        i8086::setBL(vcc);
        i8086::setBH(0x00); // no inactive display reported
        return;
      }

      // AL = 01h (Set VCC) or any other subfunction -> no-op for EGA
      break;
    }
#endif

    default:
      printf("ega: Unhandled int 10h (AH=%02x)\n", AH);
      break;
  }
}

// --- I/O ports ---

uint8_t EGA::readPort(uint16_t port)
{
//printf("EGA: READ PORT 0x%04x\n", port);
  switch (port) {

    // Attribute Controller Index
    case EGA_PORT_ATTR:
      return m_attrIndex;

    case 0x03C1: // Attribute Controller Data
      return m_attr[m_attrIndex];

    // Input Status #0
    case EGA_PORT_INSTATE0:
      return 0x9C;

    // VGA Enable
    case 0x03C3:
      return 0x00;

    // Sequencer Index
    case EGA_PORT_SEQ_IDX:
      return m_seqIndex;

    // Sequencer Data
    case EGA_PORT_SEQ_DATA:
      return m_seq[m_seqIndex];

    // Miscellaneous Output
    case 0x03CC:
      return m_miscOutput;

    // Graphics Controller Index
    case EGA_PORT_GC_IDX:
      return m_gcIndex;

    // Graphics Controller Data
    case EGA_PORT_GC_DATA:
      return m_gc[m_gcIndex];

    // CRTC Index
    case 0x03B4: // Mono base
    case EGA_PORT_CRTC_IDX:
      return m_crtcIndex;

    // CRTC Data
    case 0x03B5: // Mono base
    case EGA_PORT_CRTC_DATA:
      // Registers readable:
      // 0x0C Start Address High
      // 0x0D Start Address Low
      // 0x0E Cursor Location High
      // 0x0F Cursor Location Low
      // 0x10 Light Pen High
      // 0x11 Light Pen Low
      return m_crtc[m_crtcIndex];

    // CGA Mode Control Register
    case EGA_CGA_PORT_MODECTRL:
      return m_cga_modeControl;

    // CGA Color Select Register
    case EGA_CGA_PORT_COLORSEL:
      return m_cga_colorSelect;

    // Input Status #1 and reset AC flip-flop
    case 0x03BA: // Mono base
    case EGA_PORT_INSTATE1:
    case 0x03DB: // Some EGA/VGA implementations decode 0x3DB as an alias/mirror of the Input Status #1 at color base.
      return readInputStatus1();

    default:
      printf("ega: Unhandled read port (0x%04x)\n", port);
      return 0xFF;
  }
}

void EGA::writePort(uint16_t port, uint8_t value)
{
  switch (port) {

    // Feature Control Register (mono)
    // Monochrome emulation selected in Miscellaneous Output: I/O Address Select
    case MDA_PORT_FEATCTRL:
      // b0..1  Feature Control Bit
      m_featureControl = value & 0x03;
      break;

    // Attribute Controller Index/Data
    case EGA_PORT_ATTR:
      if (!m_attrFlipFlop) {
        m_attrIndex = value & 0x1F; // Attribute Controller Index
      } else {
        m_attr[m_attrIndex] = value; // Attribute Controller Data

        switch (m_attrIndex) {

          case 0x00 ... 0x0F: // Palette Register
            // bits 0..4 : Attribute Address
            // bit  5    : Palette Address Source
            m_paletteMap[m_attrIndex] = m_attr[m_attrIndex] & 0x3F;
            //TODO
            break;

          case 0x10: // Mode Control
            // bit 0 : Graphics/Alphanumeric Mode (0 Alphanumeric, 1 Graphics)
            // bit 1 : Display Type (0 Color, 1 Monochrome)
            // bit 2 : Enable Line Graphics Character Codes
            // bit 3 : Select Background Intensity / Enable Blink
            m_blinkEnabled = (m_attr[m_attrIndex] & 0x08) != 0;
            m_dirty = true;
            break;

          case 0x11: // Overscan Color
            // nothing to do
            break;

          case 0x12: // Color Plane Enable
            // bits 0..3 : Enable Color Plane
            // bit  4,5  : Video Status MUX
            m_colorPlaneEnable = m_attr[m_attrIndex] & 0x0F;
            // Prints a message when not all planes are enabled
            if (m_colorPlaneEnable != 0x0F)
              printf("ega: color plane enable = 0x%02x\n", m_attr[m_attrIndex]);
            break;

          case 0x13: // Horizontal Pel (Picture Elements, Pixels) Panning
            // bits 0..3 : Horizontal Pel Panning
            m_hPan = m_attr[m_attrIndex] & 0x0F;
            printf("ega: horizontal pen panning = 0x%02x <------------------------------\n", m_hPan);
            break;

          default:
            printf("ega: Unhandled attribute (0x%02x)\n", m_attrIndex);
            break;
        }
        m_dirty = true;
      }
      m_attrFlipFlop = !m_attrFlipFlop;
      break;

    // Bug-tolerance EGA BIOS
    case 0x03C1:
      if (m_attrFlipFlop) {
        writePort(0x03C0, value);
      }
      break;

    // Miscellaneous Output
    case EGA_PORT_MISC:
      // bit  0    : I/O Address Select (0 CRTC addresses to 3Bx and Input Status Register 1 to 3BA for monochrome emulation)
      // bit  1    : Enable Ram (0 disabled, 1 enabled)
      // bits 2..3 : Clock Select 0 and 1
      // bit  4    : Disable Internal Video Drivers
      // bit  5    : Page Bit for Odd/Even
      // bits 6..7 : Horizontal and Vertical Retrace Polarity
      m_miscOutput = value;
      // Update BDA
      s_ram[0x465] = m_miscOutput;
      m_vramEnabled = (m_miscOutput & 0x02) != 0;
      printf("ega: Miscellaneous output = 0x%02x\n", value);
      break;

    // Sequencer Index
    case EGA_PORT_SEQ_IDX:
      m_seqIndex = value & 0x07;
      break;

    // Sequencer Data
    case EGA_PORT_SEQ_DATA:
    {
      m_seq[m_seqIndex] = value;
      switch (m_seqIndex) {

        case EGA_SEQ_RESET: // Reset
          // bit 0 : Asynchronous Reset
          // bit 1 : Synshronous Reset
          break;

        case EGA_SEQ_CLOCKMODE: // Clocking Mode
          break;

        case EGA_SEQ_MAPMASK: // Map Mask
          // bits 0..3 : 1 Enables Map i
          m_mapMask = value & 0x0F;
          break;

        case EGA_SEQ_CHARMAPSEL: // Character Map Select
          break;

        // The EGA sequencer memory mode affects how CPU addresses map to video memory.
        // Odd/even addressing is required for full CGA compatibility.
        case EGA_SEQ_MEMMODE: // Memory Mode
          // bit 0 : Alpha (0 non-alpha mode, 1 alpha mode is active)
          // bit 1 : Extended Memory (1 memory expansion card installed)
          // bit 2 : Odd/Even (0 when even addressses->maps 0 and 2, and odd addresses -> maps 1 and 3)
          m_oddEvenAddressing = (value & 0x04) != 0;
          printf("ega: Seq[0x%02x] = 0x%02x (Memory Mode)\n", m_seqIndex, value);
          printf("ega: m_oddEvenAddressing = %d\n", m_oddEvenAddressing);
          break;
      }
      break;
    }

    // Graphics Controller Index
    case EGA_PORT_GC_IDX:
      m_gcIndex = value & 0x0F;
      break;

    // Graphics Controller Data
    case EGA_PORT_GC_DATA:
    {
      m_gc[m_gcIndex] = value;
      switch (m_gcIndex) {

        case EGA_GC_SETRESET: // Set/Reset
          // bits 0..3 : Set/Reset Bit (planes 0..3)
          m_gc_setReset = value & 0x0F;
          //printf("ega: Set/Reset = 0x%02x\n", value);
          break;

        case EGA_GC_ENABLESR: // Enable Set/Reset
          // bits 0..3 : Enable Set/Reset Bit (planes 0..3)
          m_gc_enableSetReset = value & 0x0F;
          //printf("ega: Enable Set/Reset = 0x%02x\n", value);
          break;

        case EGA_GC_COLORCMP: // Color Compare
          // bits 0..3 : Color Compare
          m_gc_colorCompare = value & 0x0F;
          //printf("ega: Color Compare = 0x%02x\n", value);
          break;

        case EGA_GC_DATAROTATE: // Data Rotate
          // bits 0..2 : Rotate Count
          // bits 3..4 : Function Select
          m_gc_rotateCount = value & 0x07;
          m_gc_functionSelect = (value >> 3) & 0x03;
          //printf("ega: Data Rotate = 0x%02x\n", value);
          break;

        case EGA_GC_READMAPSEL: // Read Map Select
          // bits 0..2 : Map Select
          // These bits represent a binary encoded value of the memory plane number
          // from which the processor reads data
          m_gc_readMapSel = value & 0x03;
          //printf("ega: Read Map Select = 0x%02x\n", value);
          break;

        case EGA_GC_MODEREG: // Mode Register
          // bits 0,1 : Write Mode
          // bit  2   : Test Condition
          // bit  3   : Read Mode
          // bit  4   : Odd/Even
          // bit  5   : Shift Register Mode 
          m_gc_writeMode = value & 0x03;
          m_gc_readMode = (value >> 3) & 0x01;
          //printf("ega: Mode Register = 0x%02x\n", value);
          break;

        case EGA_GC_MISC: // Miscellaneous Register
          // bit  0   : Graphics Mode
          // bit  1   : Chain odd map to even (CGA compatibility)
          // bits 2,3 : Memory Map Select (memory maps 0xA0000/0xB0000/0xB8000)
          //TODOm_graphicsMode = (value & 0x01) != 0;
          m_gc_memoryMapSel = (value >> 2) & 0x03;
          switch (m_gc_memoryMapSel) {
            case 0:
              m_basePhysAddr = 0xA0000;
              break;
            case 1:
              m_basePhysAddr = 0xA0000;
              break;
            case 2:
              m_basePhysAddr = 0xB0000;
              break;
            case 3:
              m_basePhysAddr = 0xB8000;
              break;
            default:
              m_basePhysAddr = 0xA0000;
              break;
          }
          printf("ega: GC[0x%02x] = 0x%02x (Miscellaneous)\n", m_gcIndex, value);
          break;

        case EGA_GC_COLORDONTC: // Color Don't Care
          // bits 0..3 : Color Plana 0..3 Don't Care
          m_gc_colorDontCare = value & 0x0F;
          //printf("ega: Color Don't Care = 0x%02x\n", value);
          break;

        case EGA_GC_BITMASK: // Bit Mask;
          m_gc_bitMask = value;
          //printf("ega: Bit Mask = 0x%02x\n", value);
          break;

        default:
          printf("ega: Unhandled gc[0x%02x]=0x%02x\n", m_gcIndex, value);
          break;
      }
      break;
    }

    // CRTC Index
    case 0x03B4: // Mono base
    case EGA_PORT_CRTC_IDX:
      m_crtcIndex = value & 0x1F;
      break;

    // CRTC Data
    case 0x03B5: // Mono base
    case EGA_PORT_CRTC_DATA:
    {
      m_crtc[m_crtcIndex] = value;
      switch (m_crtcIndex) {

        case 0x00: // Horizontal Total
        case 0x01: // Horizontal Display Enable End
        case 0x02: // Start Horizontal Blanking
        case 0x03: // End Horizontal Blanking
        case 0x04: // Start Horizontal Retrace Pulse
        case 0x05: // End Horizontal Retrace
        case 0x06: // Vertical Total
        case 0x07: // CRT Controller Overflow
        case 0x08: // Preset Row Scan
        case 0x09: // Maximum Scan Line
          printf("ega: crtc[0x%02x]=0x%02x\n", m_crtcIndex, value);
          break;

        case EGA_CRTC_CURSORSTART: // Cursor Start
          // bits 0..3 : Row Scan Cursor Begins
          m_cursorStart = m_crtc[m_crtcIndex] & 0x1F;
          m_dirty = true;
          break;

        case EGA_CRTC_CURSOREND: // Cursor End
          // bits 0..3 : Row Scan Cursor Ends
          m_cursorEnd = m_crtc[m_crtcIndex] & 0x1F;
          m_dirty = true;
          break;

        case EGA_CRTC_STARTADDR_HI: // Start Address High
        case EGA_CRTC_STARTADDR_LO: // Start Address Low
        {
          const uint16_t addr_hi = (uint16_t) m_crtc[0x0C] << 8;
          const uint16_t addr_lo = (uint16_t) m_crtc[0x0D];
          const uint16_t addr = addr_hi | addr_lo; 
          const uint16_t oldAddr = m_startAddress;

          // EGA hardware interprets the CRTC start address differently in text and graphics modes
          if (m_textMode) {
            // Text modes: the address is in character units (2 bytes per entry)
            m_startAddress = (uint32_t) addr << 1;
          } else {
            // Graphics modes: the address is a byte index into the selected 64KB memory map
            m_startAddress = (uint32_t) addr;
          }
          // Additionally, EGA wraps addressing inside the active 64KB map window
          m_startAddress &= 0xFFFF; //TODO
          if (m_startAddress != oldAddr) {
            printf("ega: crtc[0x%02x]=0x%02x (Start Address)\n", m_crtcIndex, value);
            printf("ega: Start address = 0x%04x\n", m_startAddress);
          }
          m_dirty = true;
          break;
        }

        case EGA_CRTC_CURSORPOS_HI: // Cursor Location High
        case EGA_CRTC_CURSORPOS_LO: // Cursor Location Low
        {
          uint16_t loc_hi = (uint16_t) m_crtc[0x0E] << 8;
          uint16_t loc_lo = (uint16_t) m_crtc[0x0F]; 
          uint16_t location = loc_hi | loc_lo;

          uint8_t row = location / m_textCols;
          uint8_t col = location % m_textCols;
          m_cursorRow[m_activePage] = row;
          m_cursorCol[m_activePage] = col;

          m_dirty = true;
          break;
        }

        case 0x10: // Vertical Retrace Start
        case 0x11: // Vertical Retrace End
        case 0x12: // Vertical Display Enable End
          printf("ega: crtc[0x%02x]=0x%02x\n", m_crtcIndex, value);
          break;

        case EGA_CRTC_OFFSET: // Offset
          m_lineOffset = (uint16_t) m_crtc[m_crtcIndex] * 2;
          printf("ega: crtc[0x%02x]=0x%02x (Line Offset)\n", m_crtcIndex, value);
          break;

        case 0x14: // Underline Location
        case 0x15: // Start Vertical Blanking
        case 0x16: // End Vertical Blanking
          printf("ega: crtc[0x%02x]=0x%02x\n", m_crtcIndex, value);
          break;

        case EGA_CRTC_MODECTRL: // Mode Control Register
          // bit 0 : Compatibility Mode Support
          // bit 1 : Select Row Scan Counter
          // bit 2 : Horizontal Retrace Select
          // bit 3 : Count by Two
          // bit 4 : Output Control
          // bit 5 : Address Wrap
          // bit 6 : Word/Byte Mode
          // bit 7 : Hardware Reset
          printf("ega: crtc[0x%02x]=0x%02x (Mode Control)\n", m_crtcIndex, value);
          break;

        case 0x18: // Line Compare Register
          printf("ega: crtc[0x%02x]=0x%02x\n", m_crtcIndex, value);
          break;

        default:
          printf("ega: Unhandled crtc[0x%02x]=0x%02x\n", m_crtcIndex, value);
          break;
      }
      break;
    }

    // CGA Mode Control Register Breakdown
    case EGA_CGA_PORT_MODECTRL:
    {
      uint8_t mode;

      printf("ega: mode control (CGA-legacy)\n");
      m_cga_modeControl = value;
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
    case EGA_CGA_PORT_COLORSEL:
      printf("ega: color select (CGA-legacy)\n");
      m_cga_colorSelect = value;
      m_video->updateLUT();
      break;

    // Feature Control Register (color)
    case EGA_PORT_FEATCTRL:
      // bits 0,1 : Feature Control Bit
      m_featureControl = value & 0x03;
      //TODO m_attrFlipFlop
      break;

    default:
      printf("ega: Unhandle write port (%04x=%02x)\n", port, value);
      break;
  }
}

uint8_t EGA::readInputStatus1()
{
  // Reset AC flip-flop (reading ISR1 forces 3C0h to index state)
  m_attrFlipFlop = false;

  updateVSyncApprox();

  // Ensure VSYNC bit reflects current vblank state
  if (m_inputStatus1 & 0x08) m_inputStatus1 |= 0x08;
  else                       m_inputStatus1 &= ~0x08;

  // Toggle bit 0 each read (helps software that polls ISR1)
  m_inputStatus1 ^= 0x01;

  // Optionally set bit 7 = 1 to mirror common readback behaviors
  m_inputStatus1 |= 0x80;

  m_frameCounter++;
  return m_inputStatus1;
}

void EGA::updateVSyncApprox()
{
  const uint32_t period = 20;
  const uint32_t high   = 2;
  bool vblank = (m_frameCounter % period) < high;

  if (vblank)  m_inputStatus1 |=  0x08;
  else         m_inputStatus1 &= ~0x08;
}

// --- Video Memory ---

uint8_t IRAM_ATTR EGA::readMem8(uint32_t physAddr)
{
  if (!m_vramEnabled)
    return 0xFF;

  if (m_textMode) {
    // if (!vram_isPhysAddrInRange(physAddr, 0xB8000, 0xBFFFF)
    //   return 0xFF;
    size_t offset = physAddr - 0xB8000;
    if (offset < m_planeSize) {
      return m_plane[0][offset];
    } else {
      return 0xFF;
    }
  } else {
    // if (!vram_inWindow(physAddr))
    //   return 0xFF;

    return vram_read(physAddr);
  }
}

uint16_t IRAM_ATTR EGA::readMem16(uint32_t physAddr)
{
  const uint16_t word_lo = (uint16_t) readMem8(physAddr);
  const uint16_t word_hi = (uint16_t) readMem8(physAddr + 1) << 8;
  return word_hi | word_lo;
}

void IRAM_ATTR EGA::writeMem8(uint32_t physAddr, uint8_t value)
{
  if (m_vramEnabled) {
    if (m_textMode) {
      // if (!vram_isPhysAddrInRange(physAddr, 0xB8000, 0xBFFFF)
      //   return;

      size_t offset = physAddr - 0xB8000;
      if (offset < m_planeSize) {
        m_plane[0][offset] = value;
        m_dirty = true;
      }
    } else {
      // if (!vram_inWindow(physAddr))
      //   return;

      vram_write(physAddr, value);
      m_dirty = true;
    }
  }
}

void IRAM_ATTR EGA::writeMem16(uint32_t physAddr, uint16_t value)
{
  writeMem8(physAddr,     (uint8_t) value & 0xFF);
  writeMem8(physAddr + 1, (uint8_t) (value >> 8) & 0xFF);
}

// IBM EGA exposes VRAM to the CPU in 64KB windows depending on memory map select.
// The planar storage is 128KB, but CPU addressing is limited to 64KB at a time.
// Offset must wrap on 64KB boundaries, not plane size boundaries.
size_t IRAM_ATTR EGA::vram_offsetFromPhysAddr(uint32_t phys) const
{
  // 64KB window size per EGA memory map
  // uint32_t windowSize = 0x4000; //0x10000;

  uint32_t offset = phys - m_basePhysAddr;
//if (offset > m_planeSize)
//  printf("ega: WARNING! VRAM memory offset = %lu (%lu)\n", offset, m_startAddress);
  return offset & (m_planeSize - 1);
}

inline bool IRAM_ATTR EGA::vram_inWindow(uint32_t physAddr) const
{
  return (physAddr >= m_basePhysAddr) && (physAddr <= (m_basePhysAddr + 0xFFFFu));
}

inline bool IRAM_ATTR EGA::vram_isPhysAddrInRange(uint32_t physAddr, uint32_t lo, uint32_t hi) const
{
  return (physAddr >= lo) && (physAddr <= hi);
}

uint8_t IRAM_ATTR EGA::vram_read(uint32_t addr)
{
  size_t offset = vram_offsetFromPhysAddr(addr);

// In odd/even mode, CPU address bit A0 selects which map to write,
// and the byte address is formed by dropping A0 before indexing the map.
// Each map uses a 16 KB section (0x4000 bytes) in compatibility modes.

  // In odd/even mode, CPU A0 selects the plane (even->plane 0, odd->plane 1),
  // and the byte offset ignores A0 (i.e., offset uses addr with A0 cleared).
  int oddEvenPlane = -1;
/*  if (m_oddEvenAddressing) {
      oddEvenPlane = int(addr & 0x1);    // 0 or 1
      offset &= ~size_t(1);              // drop A0 for the byte offset
  }
*/

#if 0
  for (int p = 0; p < 4; p++) { // for each plane
    m_latch[p] = m_plane[p][offset];
  }
#else
  uint8_t p0 = m_plane[0][offset];
  uint8_t p1 = m_plane[1][offset];
  uint8_t p2 = m_plane[2][offset];
  uint8_t p3 = m_plane[3][offset];
  m_latch[0] = p0;
  m_latch[1] = p1;
  m_latch[2] = p2;
  m_latch[3] = p3;
#endif

  if (m_gc_readMode == 0) {
    uint8_t plane = (m_oddEvenAddressing && (oddEvenPlane >= 0))
                      ? (uint8_t) oddEvenPlane
                      : m_gc_readMapSel;
    return m_latch[plane];
  } else {
#if 0
    uint8_t value = 0;

    for (int bit = 7; bit >= 0; --bit) {
      uint8_t pixel = (((m_latch[3] >> bit) & 1) << 3) |
                      (((m_latch[2] >> bit) & 1) << 2) |
                      (((m_latch[1] >> bit) & 1) << 1) |
                      (((m_latch[0] >> bit) & 1) << 0);

      bool match = ((pixel ^ m_gc_colorCompare) & m_gc_colorDontCare) == 0;
      value |= uint8_t(match ? 1 : 0) << bit;
    }

    return value;
#else
    // Read Mode 1 vectorizado (SIN bucle)
    uint8_t m = 0xFF;
    uint8_t cc = m_gc_colorCompare;
    uint8_t dc = m_gc_colorDontCare;

    if (dc & 0x01) m &= (cc & 0x01) ? p0 : (uint8_t)~p0;
    if (dc & 0x02) m &= (cc & 0x02) ? p1 : (uint8_t)~p1;
    if (dc & 0x04) m &= (cc & 0x04) ? p2 : (uint8_t)~p2;
    if (dc & 0x08) m &= (cc & 0x08) ? p3 : (uint8_t)~p3;
    return m;
#endif
  }
}

void IRAM_ATTR EGA::vram_write(uint32_t addr, uint8_t value)
{
  size_t offset = vram_offsetFromPhysAddr(addr);

  // Odd/Even CPU addressing (Sequencer index 4, bit 2)
  uint8_t effectiveMapMask = m_mapMask;
/*
  if (m_oddEvenAddressing) {
    const int plane = addr & 1;                // even->0, odd->1
    effectiveMapMask = (uint8_t) (1 << plane); // force a single plane
    offset &= ~size_t(1);                      // drop A0 from the byte offset
  }
*/

#if 0
  uint8_t rotated = (value >> m_gc_rotateCount) |
                    (value << (8 - m_gc_rotateCount));
#else
  uint8_t rotated;
  if (m_gc_rotateCount) {
    rotated = (value >> m_gc_rotateCount) |
              (value << (8 - m_gc_rotateCount));
  } else {
    rotated = value;
  }
#endif

  // if (m_gc_writeMode == 1)
  if ( (m_gc_writeMode == 2) ||
       (m_gc_writeMode == 3) ||
      ((m_gc_writeMode == 0) && ((m_gc_functionSelect != 0) || (m_gc_bitMask != 0xFF))) ) {
#if 0
    vram_read(addr); // Hardware behavior: latches come from a read
#else
/*  uint8_t p0 = m_plane[0][offset];
  uint8_t p1 = m_plane[1][offset];
  uint8_t p2 = m_plane[2][offset];
  uint8_t p3 = m_plane[3][offset];
  m_latch[0] = p0;
  m_latch[1] = p1;
  m_latch[2] = p2;
  m_latch[3] = p3;*/
#endif
  }

  for (int p = 0; p < 4; p++) { // for each plane
    // Check memory map mask
    if ((effectiveMapMask & (1 << p)) == 0) {
      continue;
    }

    uint8_t result = 0;

    switch (m_gc_writeMode) {
#if 0
      case 0b00:
      {
        // Each memory plane is written with the processor data rotated
        // by the number of counts in the rotate register,
        // unless Set/Reset is enabled for the plane.
        uint8_t src = rotated;

        // Planes for which Set/Reset is enabled are written with 8 bits
        // of the value contained in the Set/Reset register for that plane.
        if (m_gc_enableSetReset & (1 << p)) {
          src = ((m_gc_setReset >> p) & 1) ? 0xFF : 0x00;
        } else {
          src = applyLogicalOp(src, m_latch[p]);
        }
        result = (src & m_gc_bitMask) | (m_latch[p] & ~m_gc_bitMask);
        break;
      }
#else
      case 0b00:
      {
        uint8_t src;
        if (m_gc_enableSetReset & (1 << p)) {
          src = ((m_gc_setReset >> p) & 1) ? 0xFF : 0x00;
        } else {
          src = rotated;
        }

        // La operación lógica se aplica SIEMPRE al valor elegido (src) y al latch
        uint8_t res = applyLogicalOp(src, m_latch[p]);

        // Luego se aplica la máscara de bits
        result = (res & m_gc_bitMask) | (m_latch[p] & ~m_gc_bitMask);
        break;
      }
#endif

      case 0b01:
      {
#if 1
        // Each memory plane is written with the contents of the processor latches.
        // These latches are loaded by a processor read operation.
        result = m_latch[p];
#else
        result = (m_latch[p]         &  m_gc_bitMask) |
                 (m_plane[p][offset] & ~m_gc_bitMask);
#endif
        break;
      }

      case 0b10:
      {
        // Memory plane n (0 through 3) is filled with 8 bits of the value of data bit n
        uint8_t src = ((rotated >> p) & 1) ? 0xFF : 0x00;
        //uint8_t src = ((value >> p) & 1) ? 0xFF : 0x00;
/* TODO: En el hardware real de la EGA, el Modo 2 sí aplica la operación lógica (AND, OR, XOR)
   definida en el registro Data Rotate. Aunque muchos manuales simplificados dicen que no,
   la lógica interna de la tarjeta pasa el valor expandido (src) por la ALU antes de aplicar la máscara de bits.
   Por lo tanto, tu código comentado era más fiel al hardware original
*/
        src = applyLogicalOp(src, m_latch[p]);
        result = (src & m_gc_bitMask) | (m_latch[p] & ~m_gc_bitMask);
        break;
      }

      case 0b11: // Modified mask
      {
        // El Bit Mask se filtra con los datos rotados de la CPU
        uint8_t effectiveMask = m_gc_bitMask & rotated;
        uint8_t src = ((m_gc_setReset >> p) & 1) ? 0xFF : 0x00;

        src = applyLogicalOp(src, m_latch[p]);
        result = (src & effectiveMask) | (m_latch[p] & ~effectiveMask);
        break;
      }
      default:
        result = m_latch[p];
        break;
    }

    m_plane[p][offset] = result;
  }
}

uint8_t IRAM_ATTR EGA::applyLogicalOp(uint8_t src, uint8_t latch) const
{
  switch (m_gc_functionSelect)
  {
    case 0: // Data unmodified
      return src;
    case 1: // Data AND'ed with latched data
      return src & latch;
    case 2: // Data OR'ed with latched data
      return src | latch;
    case 3: // Data XOR'ed with latched data
      return src ^ latch;
    default:
      return src;
  }
}

void EGA::setMode(uint8_t mode)
{
  m_currentMode = mode;
  switch (m_currentMode) {

    case CGA_MODE_TEXT_40x25_16COLORS:
    case CGA_MODE_TEXT_40x25_16COLORS_ALT:
      memcpy(m_seq,  ega_seq_00,  0x05);
      memcpy(m_crtc, ega_crtc_00, 0x19);
      memcpy(m_gc,   ega_gc_00,   0x09);
      memcpy(m_attr, ega_attr_00, 0x14);
      m_miscOutput = misc_output_00;
      break;

    case CGA_MODE_TEXT_80x25_16COLORS:
    case CGA_MODE_TEXT_80x25_16COLORS_ALT:
      memcpy(m_seq,  ega_seq_02,  0x05);
      memcpy(m_crtc, ega_crtc_02, 0x19);
      memcpy(m_gc,   ega_gc_02,   0x09);
      memcpy(m_attr, ega_attr_02, 0x14);
      m_miscOutput = misc_output_02;
      break;

    case CGA_MODE_GFX_320x200_4COLORS:
    case CGA_MODE_GFX_320x200_4COLORS_ALT:
      memcpy(m_seq,  ega_seq_04,  0x05);
      memcpy(m_crtc, ega_crtc_04, 0x19);
      memcpy(m_gc,   ega_gc_04,   0x09);
      memcpy(m_attr, ega_attr_04, 0x14);
      m_miscOutput = misc_output_04;
      break;

    case CGA_MODE_GFX_640x200_2COLORS:
      memcpy(m_seq,  ega_seq_06,  0x05);
      memcpy(m_crtc, ega_crtc_06, 0x19);
      memcpy(m_gc,   ega_gc_06,   0x09);
      memcpy(m_attr, ega_attr_06, 0x14);
      m_miscOutput = misc_output_06;
      break;

    case EGA_MODE_GFX_320x200_16COLORS:
      memcpy(m_seq,  ega_seq_0D,  0x05);
      memcpy(m_crtc, ega_crtc_0D, 0x19);
      memcpy(m_gc,   ega_gc_0D,   0x09);
      memcpy(m_attr, ega_attr_0D, 0x14);
      m_miscOutput = misc_output_0D;
      break;

    case EGA_MODE_GFX_640x200_16COLORS:
      memcpy(m_seq,  ega_seq_0E,  0x05);
      memcpy(m_crtc, ega_crtc_0E, 0x19);
      memcpy(m_gc,   ega_gc_0E,   0x09);
      memcpy(m_attr, ega_attr_0E, 0x14);
      m_miscOutput = misc_output_0E;
      break;

    case MDA_MODE_GFX_720x348_MONO:
      memcpy(m_seq,  ega_seq_0F,  0x05);
      memcpy(m_crtc, ega_crtc_0F, 0x19);
      memcpy(m_gc,   ega_gc_0F,   0x09);
      memcpy(m_attr, ega_attr_0F, 0x14);
      m_miscOutput = misc_output_0F;
      break;

    case EGA_MODE_GFX_640x350_16COLORS:
      memcpy(m_seq,  ega_seq_10,  0x05);
      memcpy(m_crtc, ega_crtc_10, 0x19);
      memcpy(m_gc,   ega_gc_10,   0x09);
      memcpy(m_attr, ega_attr_10, 0x14);
      m_miscOutput = misc_output_10;
      break;

    default:
      m_seq[EGA_SEQ_RESET]   = 0x03;
      m_seq[EGA_SEQ_MAPMASK] = 0x0F;
      m_seq[EGA_SEQ_MEMMODE] = 0x07;

      m_gc[EGA_GC_MODEREG]   &= ~0x0B;
      m_gc[EGA_GC_MISC]       = 0x0C; // B8000 window default
      m_gc[EGA_GC_COLORDONTC] = 0x0F;
      m_gc[EGA_GC_BITMASK]    = 0xFF;

      m_attr[0x10] = 0x0C;
      m_attr[0x12] = 0x0F;
      break;
  }

  // External Registers
  m_featureControl = 0;
  m_inputStatus0 = 0;
  m_inputStatus1 = 0;

  // Sequencer
  m_mapMask           = m_seq[EGA_SEQ_MAPMASK] & 0x0F;
  m_oddEvenAddressing = (m_seq[EGA_SEQ_MEMMODE] & 0x04) != 0;

  // Graphics Controller
  m_gc_setReset       = m_gc[EGA_GC_SETRESET] & 0x0F;
  m_gc_enableSetReset = m_gc[EGA_GC_ENABLESR] & 0x0F;
  m_gc_colorCompare   = m_gc[EGA_GC_COLORCMP] & 0x0F;
  m_gc_rotateCount    = m_gc[EGA_GC_DATAROTATE] & 0x07;
  m_gc_functionSelect = (m_gc[EGA_GC_DATAROTATE] >> 3) & 0x03;
  m_gc_readMapSel     = m_gc[EGA_GC_READMAPSEL] & 0x03;
  m_gc_writeMode      = m_gc[EGA_GC_MODEREG] & 0x03;
  m_gc_readMode       = (m_gc[EGA_GC_MODEREG] >> 3) & 0x01;
  m_gc_memoryMapSel   = (m_gc[EGA_GC_MISC] >> 2) & 0x03;
  m_gc_colorDontCare  = m_gc[EGA_GC_COLORDONTC] & 0x0F;
  m_gc_bitMask        = m_gc[EGA_GC_BITMASK];

  // CRTC
  m_startAddress = ((uint32_t) m_crtc[0x0C] << 8) | m_crtc[0x0D];
  m_lineOffset = (uint16_t) m_crtc[0x13] * 2;
/*  if (m_textMode) {
    m_startAddress = m_startAddress << 1;
  }
*/

  switch (m_gc_memoryMapSel) {
    case 0:
      m_basePhysAddr = 0xA0000;
      break;
    case 1:
      m_basePhysAddr = 0xA0000;
      break;
    case 2:
      m_basePhysAddr = 0xB0000;
      break;
    case 3:
      m_basePhysAddr = 0xB8000;
      break;
    default:
      m_basePhysAddr = 0xA0000;
      break;
  }

  m_vramEnabled = (m_miscOutput & 0x02) != 0;

  // Cursor Shape and Visible
  m_cursorDisable = false;
#if 0
  m_cursorStart = 0x0D;
  m_cursorEnd = 0x0F;
#else
  m_cursorStart = 0x06;
  m_cursorEnd = 0x07;
#endif

  m_crtc[EGA_CRTC_CURSORSTART] = m_cursorStart | (m_cursorDisable ? 0x20 : 0x00);
  m_crtc[EGA_CRTC_CURSOREND]   = m_cursorEnd;

  // Attribute Controller
  m_blinkEnabled      = (m_attr[0x10] & 0x08) != 0;
  m_colorPlaneEnable  = m_attr[0x12] & 0x0F;
  m_hPan              = m_attr[0x13] & 0x0F;

  m_activePage = 0;

  // Cursor Position
  for (int i = 0; i < 8; i++) {
    m_cursorRow[i] = 0;
    m_cursorCol[i] = 0;
  }

  // Default CGA-legacy mode control and color set
  m_cga_modeControl = EGA_CGA_MC_ENABLED |
                      EGA_CGA_MC_TEXT80COLS |
                      EGA_CGA_MC_BIT7BLINK;
  m_cga_colorSelect = 0x30;

  switch (m_currentMode) {

    case CGA_MODE_TEXT_40x25_16COLORS:
    case CGA_MODE_TEXT_40x25_16COLORS_ALT:
      m_textMode = true;
      m_textRows = 25;
      m_textCols = 40;
      m_textPageSize = 0x0800; // 2048
      break;

    case CGA_MODE_TEXT_80x25_16COLORS:
    case CGA_MODE_TEXT_80x25_16COLORS_ALT:
    case MDA_MODE_TEXT_80x25_MONO:
      m_textMode = true;
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = 0x1000; // 4096
      break;

    // In graphic modes we also update the number of text rows
    // and cols to answer INT 10h AH=0x0F
    case CGA_MODE_GFX_320x200_4COLORS:
    case CGA_MODE_GFX_320x200_4COLORS_ALT:
    case EGA_MODE_GFX_320x200_16COLORS:
      m_textMode = false;
      m_textRows = 25;
      m_textCols = 40;
      m_textPageSize = 0x0800;
      break;

    case CGA_MODE_GFX_640x200_2COLORS:
    case EGA_MODE_GFX_640x200_16COLORS:
    case MDA_MODE_GFX_720x348_MONO:
    case EGA_MODE_GFX_640x350_16COLORS:
      m_textMode = false;
      m_textRows = 25;
      m_textCols = 80;
      m_textPageSize = 0x1000;
      break;

    default:
      m_textMode = false;
      break;
  }

/*
  printf("EGA MODE %02X regs:\n", m_currentMode);
  printf("  CRTC[09]=%02X (MaxScanLine)\n", m_crtc[0x09]);
  printf("  CRTC[12]=%02X (VertDispEnd)\n", m_crtc[0x12]);
  printf("  CRTC[17]=%02X (ModeCtrl)\n", m_crtc[0x17]);
  printf("  CRTC[13]=%02X (Offset words) -> %u bytes\n", m_crtc[0x13], (unsigned)m_crtc[0x13]*2);
  printf("  SEQ[04]=%02X (MemMode)\n", m_seq[0x04]);
  printf("  GC[05]=%02X (ModeReg) readMode=%u writeMode=%u\n", m_gc[0x05], (m_gc[0x05]>>3)&1, m_gc[0x05]&3);
  printf("  GC[06]=%02X (Misc) memMapSel=%u base=%05lX\n", m_gc[0x06], (m_gc[0x06]>>2)&3, (unsigned long)m_basePhysAddr);

  bool ds = (m_crtc[0x09] & 0x80) != 0;
  printf("  DoubleScan=%d (CRTC[09] bit7)\n", ds);
*/

  m_video->setMode(m_currentMode);
}

void EGA::clearScreen()
{
  // Clear video memory
#if EGA_NON_SEGMENTED_VRAM
  memset(m_vram, 0, EGA_VRAM_SIZE);
#else
  for (int i = 0; i < 4; i++)
    memset(m_plane[i], 0, m_planeSize);
#endif

  if (m_textMode) {
    const uint32_t pageBase = (uint32_t) m_activePage * m_textPageSize;
    const uint32_t totalCells = (uint32_t) m_textRows * m_textCols;

    uint8_t *mem = m_plane[0] + pageBase;

    for (uint32_t i = 0; i < totalCells; i++) {
      mem[i * 2 + 0] = 0x20;       // Space
      mem[i * 2 + 1] = m_textAttr; // Default attribute
    }
  }
  m_dirty = true;
}

void EGA::writeCharAttr(uint8_t ch, uint8_t attr, uint16_t count, uint8_t page)
{
  if (attr == 0) {
    attr = m_textAttr; // When no attribute defined use default
  }

  // Compute target cell offset and write character + attribute
  uint32_t offset = textCellOffset(page, m_cursorRow[page], m_cursorCol[page]);

  // Write 'count' copies of (ch, attr), without advancing cursor
  for (uint16_t i = 0; i < count; i++) {
    m_plane[0][offset++] = ch;
    m_plane[0][offset++] = attr;
  }

  // No cursor position updated
  m_dirty = true;
}

void EGA::writeCharOnly(uint8_t ch, uint16_t count, uint8_t page)
{
  while (count--) {
    // Compute VRAM offset
    uint32_t offset = textCellOffset(page, m_cursorRow[page], m_cursorCol[page]);

    // Write only character, DO NOT change attribute
    m_plane[0][offset] = ch;

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
void EGA::scrollUpWindow(uint8_t lines, uint8_t attr,
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

  if (lines == 0) {
    for (int row = top; row <= bottom; row++) {
      for (int col = left; col <= right; col++) {
        size_t dst = textCellOffset(m_activePage, row, col);
        m_plane[0][dst + 0] = 0x20; // blank
        m_plane[0][dst + 1] = attr; // fill attribute
      }
    }
  } else {
    if (lines > (bottom - top + 1))
      lines = bottom - top + 1;

    for (int row = top; row <= bottom - lines; row++) {
      for (int col = left; col <= right; col++) {
        size_t dst = textCellOffset(m_activePage, row, col);
        size_t src = textCellOffset(m_activePage, row + lines, col);
        m_plane[0][dst + 0] = m_plane[0][src + 0];
        m_plane[0][dst + 1] = m_plane[0][src + 1];
      }
    }

    // Fill the bottom 'lines' rows with blanks
    for (int row = bottom - lines + 1; row <= bottom; row++) {
      for (int col = left; col <= right; col++) {
        size_t dst = textCellOffset(m_activePage, row, col);
        m_plane[0][dst + 0] = 0x20;
        m_plane[0][dst + 1] = attr;
      }
    }
  }
  m_dirty = true;
}

// Scrolls a rectangular text area DOWN
void EGA::scrollDownWindow(uint8_t lines, uint8_t attr,
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

  if (lines == 0) {
    for (int row = top; row <= bottom; row++) {
      for (int col = left; col <= right; col++) {
        size_t dst = textCellOffset(m_activePage, row, col);
        m_plane[0][dst + 0] = 0x20; // blank
        m_plane[0][dst + 1] = attr; // fill attribute
      }
    }
  } else {
    if (lines > (bottom - top + 1))
      lines = bottom - top + 1;

    // Move existing lines down inside the rectangle:
    // Bottom-to-top order is required due to overlap
    for (int row = bottom; row >= top + lines; row--) {
      for (int col = left; col <= right; col++) {
        size_t dst = textCellOffset(m_activePage, row, col);
        size_t src = textCellOffset(m_activePage, row - lines, col);
        m_plane[0][dst + 0] = m_plane[0][src + 0];
        m_plane[0][dst + 1] = m_plane[0][src + 1];
      }
    }

    // Fill the top 'lines' rows with blanks
    for (int row = top; row < top + lines; row++) {
      for (int col = left; col <= right; col++) {
        size_t dst = textCellOffset(m_activePage, row, col);
        m_plane[0][dst + 0] = 0x20;
        m_plane[0][dst + 1] = attr;
      }
    }
  }
  m_dirty = true;
}

void EGA::ttyOutput(uint8_t ch, uint8_t page, uint8_t color)
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

      m_plane[0][offset    ] = ch;
      m_plane[0][offset + 1] = attr;

      m_cursorCol[page]++;
      if (m_cursorCol[page] >= m_textCols)
        newLine(page);
      break;
    }
  }

  syncCursorPos();
  m_dirty = true;
}

void EGA::writeString(uint8_t flags, uint8_t page, uint8_t attr,
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
      m_plane[0][offset + 0] = ch;
      m_plane[0][offset + 1] = at;

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
      m_plane[0][offset + 0] = ch;
      m_plane[0][offset + 1] = fixedAttr;

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

void EGA::newLine(uint8_t page)
{
  m_cursorCol[page] = 0;
  m_cursorRow[page]++;
  if (m_cursorRow[page] >= m_textRows) {
    // Scroll the page and clamp the cursor to the last row.
    scrollUp(page);
    m_cursorRow[page] = m_textRows - 1;
  }
}

void EGA::scrollUp(uint8_t page)
{
  // Move lines 1..N-1 to 0..N-2 within the given page and clear the last line
  const uint32_t lineSize = (uint32_t) m_textCols * 2;
  const uint32_t base     = (uint32_t) page * m_textPageSize;

  memmove(m_plane[0] + base,
          m_plane[0] + base + lineSize,
          (size_t)((m_textRows - 1) * lineSize));

  // Clear last line with spaces using the current attribute
  uint32_t lastLine = base + (uint32_t) (m_textRows - 1) * lineSize;
  for (uint32_t i = 0; i < (uint32_t) m_textCols; i++) {
    m_plane[0][lastLine + i * 2 + 0] = 0x20;       // Space character
    m_plane[0][lastLine + i * 2 + 1] = m_textAttr; // Default attribute
  }
}

/*
void EGA::printRegisters()
{
  printf("static const uint8_t ega_seq_00[]  = { 0x%02x", m_seq[0]);
  for (int i = 1; i < 5; i++)
    printf(", 0x%02x", m_seq[i]);
  printf(" };\n");

  printf("static const uint8_t ega_crtc_00[]  = { 0x%02x", m_crtc[0]);
  for (int i = 1; i < 25; i++)
    printf(", 0x%02x", m_crtc[i]);
  printf(" };\n");

  printf("static const uint8_t ega_gc_00[]  = { 0x%02x", m_gc[0]);
  for (int i = 1; i < 9; i++)
    printf(", 0x%02x", m_gc[i]);
  printf(" };\n");

  printf("static const uint8_t ega_attr_00[]  = { 0x%02x", m_attr[0]);
  for (int i = 1; i < 20; i++)
    printf(", 0x%02x", m_attr[i]);
  printf(" };\n");
}
*/
int EGA::crtcLineOffsetBytes() const { return int(m_crtc[0x13]) * 2; }

void EGA::syncBDA()
{
  // --- Cursor and Screen State ---
  // Current Video Mode
  // s_ram[0x449] = 0x03;
  *(uint16_t *) &s_ram[0x44A] = m_textCols;           // Columns on Screen
  *(uint16_t *) &s_ram[0x44C] = m_textPageSize;       // Page Size (bytes)
  *(uint16_t *) &s_ram[0x44E] = m_startAddress << 1;  // Current Start Address
  // Only for debug
  if ((m_startAddress << 1) != (m_activePage * m_textPageSize)) {
  	printf("cga: Start address mismatch (page=%d, size=%d, addr=%d)\n",
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

  // --- CRTC and Miscellaneous ---
  *(uint16_t *) &s_ram[0x463] = 0x03D4;          // CRTC Port Address
  s_ram[0x465] = m_miscOutput;                 // Miscellaneous Output Register
  s_ram[0x466] = m_cga_colorSelect;            // CGA Palette/Color

  // --- Specific EGA Info ---
  s_ram[0x484] = 24;                           // Rows on Screen - 1
  *(uint16_t *) &s_ram[0x485] = 8;              // Character Height

  // bits 0   : Alphanumeric Cursor (0 = blinking, 1 = static)
  // bit  1   : Monitor Mono (0 = color CGA/EGA, 1 = monochrome)
  // bit  2   : Wait for Retrace (1 = BIOS wait for retrace)
  // bit  3   : Video Active (0 = active, 1 = inactive)
  // bit  4   : Reserved (always 0)
  // bits 5,6 : Memory Amount (00 = 64KB, 01 = 128KB, 10 = 192KB, 11 = 256KB)
  // bit  7   : Clear Screen (0 = BIOS, 1 = don't clean)
  if (EGA_VRAM_SIZE >= 256 * 1024)      s_ram[0x487] = 0x60; // EGA Info
  else if (EGA_VRAM_SIZE >= 192 * 1024) s_ram[0x487] = 0x40;
  else if (EGA_VRAM_SIZE >= 128 * 1024) s_ram[0x487] = 0x20;
  else                                  s_ram[0x487] = 0x00;
  s_ram[0x488] = 0x09;                          // EGA Switches
  s_ram[0x489] = 0x00;                          // EGA Feature Bits

  // --- Pointer Table --- (0x40:00A8 Save Pointer Table)  
}

void EGA::syncCursorPos()
{
  // Update cursor position in CRTC registers
  const uint16_t pos = (uint16_t) m_cursorRow[m_activePage] * m_textCols +
                                  m_cursorCol[m_activePage];

  m_crtc[EGA_CRTC_CURSORPOS_HI] = (uint8_t) ((pos >> 8) & 0xFF);
  m_crtc[EGA_CRTC_CURSORPOS_LO] = (uint8_t) ( pos       & 0xFF);

  // Update BDA
  s_ram[0x450 + 2*m_activePage  ] = m_cursorCol[m_activePage];
  s_ram[0x450 + 2*m_activePage+1] = m_cursorRow[m_activePage];
}

// --- Video BIOS ---

void EGA::installROM()
{
  const size_t size = ega_rom[2] * 512; // ROM size
  const size_t base = 0xC0000;          // Base address

  printf("ega: Installing EGA dummy ROM (%d KB)\n", size / 1024);
  memset(s_ram + base, 0, size);
  memcpy(s_ram + base, ega_rom, sizeof(ega_rom));

  // Compute checksum of 8191 bytes
  uint8_t sum = 0;
  for (size_t i = 0; i < size - 1; i++) {
      sum += s_ram[base + i];
  }
  s_ram[base + size - 1] = (uint8_t) (0 - sum);
}

void EGA::checkROM()
{
  // Read Equipment Word (BDA:0040:0010h)
  uint16_t equipment = *(uint16_t *) &s_ram[0x0010 + 0x0400];

  // Reat Vector INT 10h (IVT: 0000:0040h)
  uint16_t int10_offset = *(uint16_t *) &s_ram[0x0040];
  uint16_t int10_segment = *(uint16_t *) &s_ram[0x0042];

  // Print results
  printf("ega: Equipment (BDA 0040:0010h) = 0x%04x\n", equipment);
  printf("ega: int 10h IVT (Segment:Offset) = 0x%04x:0x%04x\n", int10_segment, int10_offset);
}

} // end of namespace
