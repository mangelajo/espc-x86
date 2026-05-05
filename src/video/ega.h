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

#include "video/video_adapter.h"
#include "video/scanout_context.h"

#include <stdint.h>

// Total memory: 4 (planes) x 16 KB = 64 KB
//#define EGA_VRAM_SIZE  65536 // 64 KB
#define EGA_VRAM_SIZE  131072 // 128 KB
//#define EGA_VRAM_SIZE  196608
//#define EGA_VRAM_SIZE  262144 // 256 KB

// Set to 1 for a single contiguous VRAM block, or 0 to use 
// independent memory segments for each EGA color plane.
// Note: Non-segmented VRAM requires a single, large enough,
// (internal + DMA) DRAM block to be available (see init()).
#define EGA_NON_SEGMENTED_VRAM 0

// IO Ports
#define MDA_PORT_FEATCTRL      0x03BA
#define EGA_PORT_ATTR          0x03C0
#define EGA_PORT_INSTATE0      0x03C2 // Read
#define EGA_PORT_MISC          0x03C2 // Write
#define EGA_PORT_SEQ_IDX       0x03C4
#define EGA_PORT_SEQ_DATA      0x03C5
#define EGA_PORT_PALETTE       0x03C9 // placeholder VGA
#define EGA_PORT_GC_IDX        0x03CE
#define EGA_PORT_GC_DATA       0x03CF
#define EGA_PORT_CRTC_IDX      0x03D4
#define EGA_PORT_CRTC_DATA     0x03D5
#define EGA_PORT_INSTATE1      0x03DA // Read
#define EGA_PORT_FEATCTRL      0x03DA // Write

// Sequencer Registers
#define EGA_SEQ_RESET          0x00
#define EGA_SEQ_CLOCKMODE      0x01
#define EGA_SEQ_MAPMASK        0x02
#define EGA_SEQ_CHARMAPSEL     0x03
#define EGA_SEQ_MEMMODE        0x04

// Graphics Controller Registers
#define EGA_GC_SETRESET        0x00
#define EGA_GC_ENABLESR        0x01
#define EGA_GC_COLORCMP        0x02
#define EGA_GC_DATAROTATE      0x03
#define EGA_GC_READMAPSEL      0x04
#define EGA_GC_MODEREG         0x05
#define EGA_GC_MISC            0x06
#define EGA_GC_COLORDONTC      0x07
#define EGA_GC_BITMASK         0x08

// CRTC (Cathode Ray Tube Controller) Registers
#define EGA_CRTC_CURSORSTART   0x0A
#define EGA_CRTC_CURSOREND     0x0B
#define EGA_CRTC_STARTADDR_HI  0x0C
#define EGA_CRTC_STARTADDR_LO  0x0D
#define EGA_CRTC_CURSORPOS_HI  0x0E
#define EGA_CRTC_CURSORPOS_LO  0x0F
#define EGA_CRTC_OFFSET        0x13
#define EGA_CRTC_MODECTRL      0x17

// CGA-Legacy
#define EGA_CGA_PORT_MODECTRL  0x03D8
#define EGA_CGA_PORT_COLORSEL  0x03D9

#define EGA_CGA_MC_TEXT80COLS  0x01
#define EGA_CGA_MC_GRAPHICS    0x02
#define EGA_CGA_MC_MONOCHROME  0x04
#define EGA_CGA_MC_ENABLED     0x08
#define EGA_CGA_MC_HIGHRES     0x10
#define EGA_CGA_MC_BIT7BLINK   0x20

/*
 * GLaBIOS BUG: the POST treats VID=00 in EQUIP_FLAGS as "video ROM failed",
 * even though 00 normally means EGA/VGA/Option ROM. We OR the bits instead
 * of clearing them to avoid the BIOS triggering the 3-long/3-short error beep.
 * Thus, we use OR BYTE [ES:0010h], 0x20 instead of AND BYTE [ES:0010h], 0xCF
 */

// Dummy EGA VBIOS - 2KB (4 blocks of 512 bytes)
// Based on real EGA ROM structure for BDA initialization
static uint8_t ega_rom[] = {

  // --- VBIOS ROM HEADER ---
  0x55, 0xAA,             // Mandatory VBIOS signature
  0x04,                   // ROM size (4 blocks of 512 bytes)
  0xEB, 0x17,             // JMP SHORT 0x17 (Skip the string)

  // --- DATA AREA (Offset 0x05) ---
  // "EGA Dummy Video BIOS\r\n\0" (23 bytes)
  0x45, 0x47, 0x41, 0x20, 0x44, 0x75, 0x6D, 0x6D, 0x79, 0x20, 
  0x56, 0x69, 0x64, 0x65, 0x6F, 0x20, 0x42, 0x49, 0x4F, 0x53, 
  0x0D, 0x0A, 0x00,

  // --- start: Segment setup ---
  0x0E,                   // PUSH CS
  0x1F,                   // POP DS           ; DS = CS (0xC000) to access string
  0x06,                   // PUSH ES          ; Save original ES

  // --- (1) Update INT 10h Vector using ES override ---
  0x31, 0xC0,             // XOR AX, AX
  0x8E, 0xC0,             // MOV ES, AX       ; ES = 0000h (IVT)
  0x26, 0xC7, 0x06, 0x40, // MOV WORD [ES:0040h], ...
  0x00, 0x4C, 0x00,       // ... offset 0x004C (int10_handler)
  0x26, 0xC7, 0x06, 0x42, // MOV WORD [ES:0042h], ...
  0x00, 0x00, 0xC0,       // ... segment 0xC000
  0x07,                   // POP ES           ; Restore ES

  // --- (2) Update BDA Equipment Word using ES override ---
  0x06,                   // PUSH ES
  0xB8, 0x40, 0x00,       // MOV AX, 0x0040
  0x8E, 0xC0,             // MOV ES, AX       ; ES = 0040h (BDA)
  0x26, 0x80, 0x0E, 0x10, // OR BYTE [ES:0010h], ...
  0x00, 0x20,             // ... 0x20 (EGA bit)
  0x07,                   // POP ES

  // --- (3) Initialize Video Mode 3 ---
  0xB8, 0x03, 0x00,       // MOV AX, 0x0003 (Set Mode 03h)
  0xCD, 0x10,             // INT 10h

  // --- (4) Print "EGA Dummy BIOS" via TTY ---
  0xBE, 0x05, 0x00,       // MOV SI, 0x0005   ; Point to string at offset 05h
  0xB4, 0x0E,             // MOV AH, 0x0E     ; TTY Service
  // print_loop:
  0xAC,                   // LODSB            ; AL = [DS:SI], SI++
  0x84, 0xC0,             // TEST AL, AL      ; Check for null
  0x74, 0x04,             // JZ end_print     ; If zero, jump to RETF
  0xCD, 0x10,             // INT 10h          ; Print char
  0xEB, 0xF7,             // JMP print_loop   ; Repeat

  // end_print:
  0xCB,                   // RETF             ; Far return to POST
  0x90,                   // NOP              ; Padding for alignment

  // --- int10_handler: (Offset 0x004B) ---
  0xCF                    // IRET             ; int10_handler (Offset 0x4C)
};

using fabgl::RGB222;

namespace video {

class VideoScanout;

// EGA video card emulation
class EGA : public VideoAdapter,
            public ScanoutContext {

public:

   EGA();
  ~EGA();

  void init(uint8_t *ram, VideoScanout *video);
  void reset();

  // BIOS Video Interrupt (INT 10h)
  void handleInt10h();

  // I/O Ports
  uint8_t readPort(uint16_t port);
  void    writePort(uint16_t port, uint8_t value);

  // Mapped Memory
  uint8_t  readMem8(uint32_t physAddr);
  uint16_t readMem16(uint32_t physAddr);
  void writeMem8(uint32_t physAddr, uint8_t value);
  void writeMem16(uint32_t physAddr, uint16_t value);

  // Scanout Context
  bool isPlanar() const { return true; }
  uint8_t *plane(uint8_t index) override { return m_plane[index]; }
  size_t vramSize() override { return EGA_VRAM_SIZE; }

  uint32_t startAddress() override { return m_startAddress; }
  uint16_t textPageSize() override { return m_textPageSize; }

  bool cursorEnabled() override { return !m_cursorDisable; }
  uint8_t cursorStart() override { return m_cursorStart; }
  uint8_t cursorEnd() override { return m_cursorEnd; }
  uint8_t activePage() override { return m_activePage; }
  uint8_t cursorRow(uint8_t page) override { return m_cursorRow[page]; }
  uint8_t cursorCol(uint8_t page) override { return m_cursorCol[page]; }
  RGB222 paletteMap(uint8_t index, uint8_t total) override {
    return RGB222(m_palette[m_paletteMap[index]].r,
                  m_palette[m_paletteMap[index]].g,
                  m_palette[m_paletteMap[index]].b);
  }
  bool blinkEnabled() override { return isBit7Blinking(); }
  uint8_t colorPlaneEnable() override { return m_colorPlaneEnable; }

  uint32_t renderStamp() { return m_stamp; }

private:

  // --- External ---
  uint8_t *s_ram;  // Main Memory (needed to update BDA)

  VideoScanout *m_video; // Renderer

  // --- Internal ---
#if EGA_NON_SEGMENTED_VRAM
  uint8_t *m_vram; // Video Memory
#endif

  // 4 planar VRAM buffers and read latches
  uint8_t *m_plane[4];
  uint8_t  m_latch[4];

  static constexpr uint32_t m_planeSize = EGA_VRAM_SIZE >> 2;

  // Sequencer Registers
  uint8_t m_seq[5];
  uint8_t m_seqIndex;

  // CRT Controller Registers
  uint8_t m_crtc[0x19];
  uint8_t m_crtcIndex;

  // Graphics Controller Registers
  uint8_t m_gc[9];
  uint8_t m_gcIndex;

  // Attribute Controller Registers
  uint8_t m_attr[0x14];
  uint8_t m_attrIndex;
  bool    m_attrFlipFlop;

  // External Registers
  uint8_t m_miscOutput;     // Miscellaneous Output Register
  uint8_t m_featureControl; // Feature Control Register
  uint8_t m_inputStatus0;   // Input Status Register 0
  uint8_t m_inputStatus1;   // Input Status Register 1

  // Sequencer
  uint8_t m_mapMask;
  bool m_oddEvenAddressing;

  // Graphics Controller
  uint8_t m_gc_setReset;
  uint8_t m_gc_enableSetReset;
  uint8_t m_gc_colorCompare;
  uint8_t m_gc_rotateCount;
  uint8_t m_gc_functionSelect;
  uint8_t m_gc_readMapSel;
  uint8_t m_gc_writeMode;
  uint8_t m_gc_readMode;
  uint8_t m_gc_memoryMapSel;
  uint8_t m_gc_colorDontCare;
  uint8_t m_gc_bitMask;

  // CRTC
  uint32_t m_startAddress;
  uint16_t m_lineOffset; // pitch

  uint32_t m_basePhysAddr;

  // Miscellaneous
  bool m_vramEnabled;
  bool m_cursorDisable;

  // Cursor Shape
  uint8_t m_cursorStart;
  uint8_t m_cursorEnd;

  uint8_t m_activePage;

  // Cursor Position
  uint8_t m_cursorRow[8];
  uint8_t m_cursorCol[8];

  // Attribute Controller
  bool m_blinkEnabled;
  uint8_t m_colorPlaneEnable;
  uint8_t m_hPan;

  // 64-entry EGA palette and 16-entry attribute map
  struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
  };

  RGB     m_palette[64];
  uint8_t m_paletteMap[16];

  // mode state
  uint8_t  m_currentMode;
  bool     m_textMode;

  // Text mode dimensions (40x25 and 80x25)
  uint8_t  m_textRows;
  uint8_t  m_textCols;
  uint16_t m_textPageSize; // in bytes

  // Default Text Attribute (foreground 0x0F / background 0xF0)
  // Used in (INT 10h):
  // (1) Clear screen
  // (2) Scroll Up/Down (AH=06h / AH=07h) when no attribute is defined
  // (3) Write character (AH=09h) when no attribute is defined
  // (4) Teletype (AH=0Eh)
  const uint8_t m_textAttr;//TODO(EGA?)

  bool m_dirty;
  uint32_t m_stamp;

  // Frame counter used to simulate vertical retrace timing on port 0x3DA
  uint32_t m_frameCounter; // increments on each 3DA read (simple approximation)
 
  uint8_t readInputStatus1();
  void updateVSyncApprox();

  // CRTC helpers
  int      crtcLineOffsetBytes() const;

  uint8_t  applyLogicalOp(uint8_t src, uint8_t latch) const;

  // Address range helpers
  size_t   vram_offsetFromPhysAddr(uint32_t phys) const;

  inline bool vram_inWindow(uint32_t physAddr) const;
  inline bool vram_isPhysAddrInRange(uint32_t physAddr, uint32_t lo, uint32_t hi) const;

  // EGA VRAM pipeline entry points for the CPU
  uint8_t  vram_read(uint32_t addr);
  void     vram_write(uint32_t addr, uint8_t value);

  // Text helpers
  inline uint32_t textCellOffset(uint8_t page, int row, int col) const {
    // Text page base + offset
    return (uint32_t) page * m_textPageSize + (row * m_textCols + col) * 2;

  }

  // initialization helpers
  void initPalette();
  void resetRegisters();
  //void printRegisters();

  void setMode(uint8_t mode);

  // CGA Legacy
  uint8_t m_cga_modeControl;
  uint8_t m_cga_colorSelect;

  // CGA Mode Control
  inline bool isText80Columns()  const { return (m_cga_modeControl & EGA_CGA_MC_TEXT80COLS) != 0; }
  inline bool isGraphicsMode()   const { return (m_cga_modeControl & EGA_CGA_MC_GRAPHICS) != 0; }
  inline bool isHighResolution() const { return (m_cga_modeControl & EGA_CGA_MC_HIGHRES) != 0; }
  inline bool isVideoEnabled()   const { return (m_cga_modeControl & EGA_CGA_MC_ENABLED) != 0; }
  inline bool isBit7Blinking()   const { return (m_cga_modeControl & EGA_CGA_MC_BIT7BLINK) != 0; }

  void clearScreen();

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

  // Update BIOS Data Area (BDA)
  void syncBDA();

  // Syncs the cursor position with CRTC registers
  // and update BDA
  void syncCursorPos();

  // --- Video BIOS ---

  void installROM();
  void checkROM();

  uint32_t m_renderFrameCounter;
  uint32_t m_renderStartAddress;

};

} // end of namespace
