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

#include "drivers/video/vga_direct.h"
#include "fonts/fonts.h"

#include <stdint.h>

// --- Video Modes ---

// Text/CGA-compatible modes
#define CGA_MODE_TEXT_40x25_16COLORS      0x00 // 40x25 text, 16 colors
#define CGA_MODE_TEXT_40x25_16COLORS_ALT  0x01 // same as 00h, different palette attributes
#define CGA_MODE_TEXT_80x25_16COLORS      0x02 // 80x25 text, 16 colors
#define CGA_MODE_TEXT_80x25_16COLORS_ALT  0x03 // same as 02h, different palette attributes
#define CGA_MODE_GFX_320x200_4COLORS      0x04 // CGA mode 4
#define CGA_MODE_GFX_320x200_4COLORS_ALT  0x05 // CGA mode 5 (alternate palette)
#define CGA_MODE_GFX_640x200_2COLORS      0x06 // CGA high resolution

#define MDA_MODE_TEXT_80x25_MONO          0x07 // MDA compatible

// Tandy modes
#define TGA_MODE_GFX_160x200_16COLORS     0x08
#define TGA_MODE_GFX_320x200_16COLORS     0x09
#define TGA_MODE_GFX_640x200_4COLORS      0x0A

// EGA native modes
#define EGA_MODE_GFX_320x200_16COLORS     0x0D // EGA planar mode (4 planes)
#define EGA_MODE_GFX_640x200_16COLORS     0x0E // EGA planar mode
#define MDA_MODE_GFX_720x348_MONO         0x0F // MDA high-resolution monochrome
#define EGA_MODE_GFX_640x350_16COLORS     0x10 // EGA flagship color mode

// MCGA
#define MCGA_MODE_GFX_320x200_256COLORS   0x13

using fabgl::VGADirectController;
using fabgl::DrawScanlineCallback;
using fabgl::FontInfo;

namespace video {

class ScanoutContext;
class VideoScanout;

void drawOSDVolume(VideoScanout *device, int pixelsLine, int scanLines, int charScanline, int textRow, uint8_t *dst);
void drawOSDPause(VideoScanout *device, int pixelsLine, int scanLines, int charScanline, int textRow, uint8_t *dst);

// Video Digital-to-Analog Converter (DAC)
class VideoScanout {

public:

   VideoScanout();
  ~VideoScanout();

  void init();

  void setSource(ScanoutContext *context);
  void setMode(int mode);
  int getMode() { return m_currentMode; }

  void run();
  void stop();

  // true = pause, false = resume
  void pause(bool enable);

  void updateLUT();
  void setBorder(uint8_t color);

  // OSD
  void showVolume(uint8_t volume);

  uint8_t *rawSnapshot(uint16_t *width, uint16_t *height);

private:

  VGADirectController *m_VGADCtrl;

  // This flags allow the user to disable the video,
  // and then use the display to modify the emulator,
  // take a snapshot, etc.
  enum class State {
    Stopped,
    Running,
    Paused
  };

  volatile State m_state = State::Stopped;

  uint8_t m_currentMode;

  uint32_t m_frameCounter;

  uint8_t *m_rawPixelLUT;
  FontInfo m_font;
  uint8_t *m_cursorGlyph;

  // OSD: speaker volume indicator
  bool m_OSD_showVolume;
  uint8_t m_OSD_rawPixelBg;
  uint8_t m_OSD_rawPixelFgH;
  uint8_t m_OSD_rawPixelFgL;
  uint8_t m_OSD_volumeLevel; // 0..127
  uint32_t m_OSD_frame;      // frame when OSD was triggered

  DrawScanlineCallback m_callback;
  int m_scanLines; // Number of scan lines per callback
  char const *m_modeLine;

  int  m_width;
  int  m_height;

  ScanoutContext *m_context;

  // Video Memory
  uint8_t *m_vram;     // Linear (CGA)
  uint8_t *m_plane[4]; // Planar (EGA)
  uint32_t m_vramSize;

  uint32_t m_startAddress;
  uint16_t m_lineOffset;
  uint16_t m_textPageSize;
  uint8_t m_activePage;

  uint8_t m_cursorRow;
  uint8_t m_cursorCol;

  bool m_cursorEnabled;
  bool blinkEnabled;

  uint8_t m_rawBorderColor;
  uint8_t m_colorPlaneEnable; // for EGA cards

  void reallocLUT(); // Allocate memory for LUT
  void releaseLUT();

  void reallocFont(FontInfo const *font);
  void releaseFont();

  void updateCursorGlyph();
  void removeCursorGlyph();

  static void drawScanline_text_40x25(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_text_80x25(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_mda_80x25(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_cga_320x200x4(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_cga_640x200x2(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_tandy_320x200x16(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_tandy_640x200x4(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_ega_320x200x16(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_ega_640x200x16(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_mda_720x348x2(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_ega_640x350x16(void *ctx, uint8_t *dst, int scanLine);
  static void drawScanline_mcga_320x200x256(void *ctx, uint8_t *dst, int scanLine);

  friend void drawOSDVolume(VideoScanout *device, int pixelsLine, int scanLines, int charScanline, int textRow, uint8_t *dst);
  friend void drawOSDPause(VideoScanout *device, int pixelsLine, int scanLines, int charScanline, int textRow, uint8_t *dst);
};

} // end of namespace
