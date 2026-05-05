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

#include "video/video_scanout.h"
#include "video/scanout_context.h"

#include "esp_heap_caps.h"

#define FABGL_FONT_INCLUDE_DEFINITION
#include "fonts/Bm437_Amstrad_PC.h"
#include "fonts/Bm437_IBM_BIOS.h"
#include "fonts/Bm437_IBM_CGA.h"
#include "fonts/Bm437_IBM_EGA_8x8.h"
#include "fonts/Bm437_IBM_MDA.h"
#include "fonts/Bm437_IBM_VGA_8x16.h"
#include "fonts/Bm437_Tandy1K-I_200L.h"

#include <stdio.h>
#include <string.h>

#pragma GCC optimize ("O3")

using fabgl::FONT_Bm437_Amstrad_PC;
using fabgl::FONT_Bm437_IBM_BIOS;
using fabgl::FONT_Bm437_IBM_CGA;
using fabgl::FONT_Bm437_IBM_EGA_8x8;
using fabgl::FONT_Bm437_IBM_MDA;
using fabgl::FONT_Bm437_IBM_VGA_8x16;
using fabgl::FONT_Bm437_Tandy1K_I_200L;

namespace video {

// LUTs in internal RAM (DRAM)
static uint32_t* DRAM_ATTR m_egaLUT_H[4]; // Pixels 0, 1, 2, 3
static uint32_t* DRAM_ATTR m_egaLUT_L[4]; // Pixels 4, 5, 6, 7

VideoScanout::VideoScanout() :
  m_VGADCtrl(nullptr),
  m_frameCounter(0),
  m_rawPixelLUT(nullptr),
  m_cursorGlyph(nullptr),
  m_OSD_showVolume(false),
  m_context(nullptr),
  m_vram(nullptr),
  m_startAddress(0),
  m_textPageSize(0),
  m_activePage(0),
  m_cursorRow(0),
  m_cursorCol(0),
  m_cursorEnabled(false),
  blinkEnabled(false),
  m_colorPlaneEnable(0x0F)
{
  m_font.data = nullptr;
}

VideoScanout::~VideoScanout()
{
  if (m_VGADCtrl) {

  	// Video stop
    stop();

    // Free memory
    releaseLUT();
    releaseFont();
    removeCursorGlyph();

    delete m_VGADCtrl;
  }
}

void VideoScanout::init()
{
  // Create video controller (without autorun)
  m_VGADCtrl = new VGADirectController(false);
  m_VGADCtrl->begin();
}

void VideoScanout::setSource(ScanoutContext *context)
{
  m_context = context;
  if (m_context->isPlanar()) {
    // EGA and Tandy Planar Memories
    for (int i = 0; i < 4; i++) {
      m_plane[i] = m_context->plane(i);
    }
    // CGA-legacy (in text modes only the first plane is used)
    m_vram = m_plane[0];
  } else {
    m_vram = m_context->vram();
  }
  m_vramSize = m_context->vramSize();
}

void VideoScanout::setMode(int mode)
{
  m_currentMode = mode;

  printf("video: Set video mode 0x%02x\n", mode);
  switch(mode) {

    case CGA_MODE_TEXT_40x25_16COLORS:
    case CGA_MODE_TEXT_40x25_16COLORS_ALT:
      m_width = 320;
      m_height = 240;
      m_callback = drawScanline_text_40x25;
      m_scanLines = 4;
      m_modeLine = QVGA_320x240_60Hz;
      reallocFont(&FONT_Bm437_Amstrad_PC);
      updateCursorGlyph();
      break;

    case CGA_MODE_TEXT_80x25_16COLORS:
    case CGA_MODE_TEXT_80x25_16COLORS_ALT:
      m_width = 640;
      m_height = 240;
      m_callback = drawScanline_text_80x25;
      m_scanLines = 4;
      m_modeLine = VGA_640x240_60Hz;
      if (m_context->isPlanar()) {
        reallocFont(&FONT_Bm437_IBM_EGA_8x8);
      } else {
        reallocFont(&FONT_Bm437_Amstrad_PC);
      }
      updateCursorGlyph();
      break;

    case MDA_MODE_TEXT_80x25_MONO:
      m_width = 640;
      m_height = 400;
      m_callback = drawScanline_mda_80x25;
      m_scanLines = 8;
      m_modeLine = VGA_640x480_60Hz;
      // MDA no usa páginas de 32 KiB (texto ocupa ~4 KiB).
      // Blink de atributo bit7: usa el mismo flag que en CGA si tu backend lo soporta.
      reallocFont(&FONT_Bm437_IBM_VGA_8x16);
      updateCursorGlyph();
      break;

    case CGA_MODE_GFX_320x200_4COLORS:
    case CGA_MODE_GFX_320x200_4COLORS_ALT:
      m_width = 320;
      m_height = 200;
      m_callback = drawScanline_cga_320x200x4;
      m_scanLines = 1;
      m_modeLine = QVGA_320x240_60Hz;
      break;

    case CGA_MODE_GFX_640x200_2COLORS:
      m_width = 640;
      m_height = 200;
      m_callback = drawScanline_cga_640x200x2;
      m_scanLines = 1;
      m_modeLine = VGA_640x240_60Hz;
      break;

    case TGA_MODE_GFX_160x200_16COLORS:
    case TGA_MODE_GFX_320x200_16COLORS:
      m_width = 320;
      m_height = 200;
      m_callback = drawScanline_tandy_320x200x16;
      m_scanLines = 1;
      m_modeLine = QVGA_320x240_60Hz;
      break;

    case TGA_MODE_GFX_640x200_4COLORS:
      m_width = 640;
      m_height = 200;
      m_callback = drawScanline_tandy_640x200x4;
      m_scanLines = 1;
      m_modeLine = VGA_640x240_60Hz;
      break;

    case EGA_MODE_GFX_320x200_16COLORS:
      m_width = 320;
      m_height = 200;
      m_callback = drawScanline_ega_320x200x16;
      m_scanLines = 1;
      m_modeLine = QVGA_320x240_60Hz;
      break;

    case EGA_MODE_GFX_640x200_16COLORS:
      m_width = 640;
      m_height = 200;
      m_callback = drawScanline_ega_640x200x16;
      m_scanLines = 1;
      m_modeLine = VGA_640x240_60Hz;
      break;

    case MDA_MODE_GFX_720x348_MONO:
      m_width = 720;
      m_height = 348;
      m_callback = drawScanline_mda_720x348x2;
      m_scanLines = 1;
      m_modeLine = VGA_720x348_50HzD;
      break;

    case EGA_MODE_GFX_640x350_16COLORS:
      m_width = 640;
      m_height = 350;
      m_callback = drawScanline_ega_640x350x16;
      m_scanLines = 1;
      m_modeLine = VGA_640x480_60Hz;
      break;

    case MCGA_MODE_GFX_320x200_256COLORS:
      m_width = 320;
      m_height = 200;
      m_callback = drawScanline_mcga_320x200x256;
      m_scanLines = 1;
      m_modeLine = QVGA_320x240_60Hz;
      break;

    default:
      printf("video: Unexpected video mode (0x%02x)\n", m_currentMode);
      return;
  }

  reallocLUT();
  updateLUT();
}

void VideoScanout::run()
{
  m_VGADCtrl->setDrawScanlineCallback(m_callback, this);
  m_VGADCtrl->setScanlinesPerCallBack(m_scanLines);
  m_VGADCtrl->setResolution(m_modeLine, m_width, m_height);

  if ((m_VGADCtrl->getViewPortWidth() != m_width) ||
      (m_VGADCtrl->getViewPortHeight() != m_height)) {
    printf("video: Warning! Unexpected ViewPort size (%dx%d != %dx%d)\n", m_width, m_height,
      m_VGADCtrl->getViewPortWidth(),
      m_VGADCtrl->getViewPortHeight());
  }
  m_VGADCtrl->run();
  m_state = State::Running;
}

void VideoScanout::pause(bool enable)
{
  if (enable && (m_state == State::Running)) {
    m_state = State::Paused;
  } else if (!enable && (m_state == State::Paused)) {
    m_state = State::Running;
  }
}

void VideoScanout::stop()
{
  m_VGADCtrl->end();
  m_state = State::Stopped;
}

void VideoScanout::reallocLUT()
{
  if (m_state == State::Running) {
    printf("video: WARNING! Realloc LUT while running\n");
  }

  releaseLUT();

  switch (m_currentMode) {

    case CGA_MODE_TEXT_40x25_16COLORS:
    case CGA_MODE_TEXT_40x25_16COLORS_ALT:
    case CGA_MODE_TEXT_80x25_16COLORS:
    case CGA_MODE_TEXT_80x25_16COLORS_ALT:
      // Each LUT item contains half pixel (an index to 16 colors palette)
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(16, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!m_rawPixelLUT) {
        printf("video: Unable to allocate memory for the LUT!\n");
      }
      break;

    case MDA_MODE_TEXT_80x25_MONO:
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(3, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!m_rawPixelLUT) {
        printf("video: Unable to allocate memory for the LUT!\n");
      }
      break;

    case CGA_MODE_GFX_320x200_4COLORS:
    case CGA_MODE_GFX_320x200_4COLORS_ALT:
    case TGA_MODE_GFX_640x200_4COLORS:
      // Each LUT item contains four pixels (decodes as four raw bytes)
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(256 * 4, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!m_rawPixelLUT) {
        printf("video: Unable to allocate memory for the LUT!\n");
      }
      break;

    case CGA_MODE_GFX_640x200_2COLORS:
      // Each LUT item contains eight pixels (decodes as eight raw bytes)
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(256 * 8, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!m_rawPixelLUT) {
        printf("video: Unable to allocate memory for the LUT!\n");
      }
      break;

    case TGA_MODE_GFX_160x200_16COLORS:
    case TGA_MODE_GFX_320x200_16COLORS:
#if 0
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(256 * 2, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#else
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(256 * 4, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#endif
      if (!m_rawPixelLUT) {
        printf("video: Unable to allocate memory for the LUT!\n");
      }
      break;

    case EGA_MODE_GFX_320x200_16COLORS:
    case EGA_MODE_GFX_640x200_16COLORS:
      // Allocate raw pixels LUT
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(16, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!m_rawPixelLUT) {
        printf("video: Unable to allocate memory for the LUT!\n");
      }

      // Allocate EGA LUT
      for (int p = 0; p < 4; p++) {
        m_egaLUT_H[p] = (uint32_t *) heap_caps_malloc(256 * sizeof(uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        m_egaLUT_L[p] = (uint32_t *) heap_caps_malloc(256 * sizeof(uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!m_egaLUT_H[p] || !m_egaLUT_L[p]) {
          printf("video: Unable to allocate memory for the LUT! (2nd EGA LUT)\n");
        }
      }
      break;

    case MDA_MODE_GFX_720x348_MONO:
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(256 * sizeof(uint64_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!m_rawPixelLUT) {
        printf("video: Unable to allocate memory for the LUT!\n");
      }
      break;

    case EGA_MODE_GFX_640x350_16COLORS:
      break;

    case MCGA_MODE_GFX_320x200_256COLORS:
      m_rawPixelLUT = (uint8_t *) heap_caps_malloc(256, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!m_rawPixelLUT) {
        printf("video: Unable to allocate memory for the LUT!\n");
      }
      break;

    default:
      break;
  }
}

void VideoScanout::releaseLUT()
{
  if (m_rawPixelLUT) {
    heap_caps_free((void *) m_rawPixelLUT);
    m_rawPixelLUT = nullptr;
  }
  if (m_egaLUT_H[0]) {
    for (int p = 0; p < 4; p++) {
      heap_caps_free((void *) m_egaLUT_H[p]);
      heap_caps_free((void *) m_egaLUT_L[p]);
      m_egaLUT_H[p] = nullptr;
      m_egaLUT_L[p] = nullptr;
    }
  }
}

void VideoScanout::updateLUT()
{
  m_rawBorderColor = m_VGADCtrl->createRawPixel(RGB222(0, 0, 0));

  m_OSD_rawPixelBg  = m_VGADCtrl->createRawPixel(RGB222(0, 0, 0));
  m_OSD_rawPixelFgH = m_VGADCtrl->createRawPixel(RGB222(3, 3, 1));
  m_OSD_rawPixelFgL = m_VGADCtrl->createRawPixel(RGB222(2, 2, 2));

  switch (m_currentMode) {

    case CGA_MODE_TEXT_40x25_16COLORS:
    case CGA_MODE_TEXT_40x25_16COLORS_ALT:
    case CGA_MODE_TEXT_80x25_16COLORS:
    case CGA_MODE_TEXT_80x25_16COLORS_ALT:
      // Each LUT item contains half pixel (an index to 16 colors palette)
      for (int i = 0; i < 16; i++) {
        m_rawPixelLUT[i] = m_VGADCtrl->createRawPixel(m_context->paletteMap(i, 16));
      }
      break;

    case MDA_MODE_TEXT_80x25_MONO:
      m_rawPixelLUT[0] = m_VGADCtrl->createRawPixel(RGB222(0, 0, 0));
      m_rawPixelLUT[1] = m_VGADCtrl->createRawPixel(RGB222(2, 2, 2));
      m_rawPixelLUT[2] = m_VGADCtrl->createRawPixel(RGB222(3, 3, 3));
      break;

    case CGA_MODE_GFX_320x200_4COLORS:
    case CGA_MODE_GFX_320x200_4COLORS_ALT:
    case TGA_MODE_GFX_640x200_4COLORS:
    {
      uint8_t rawPixel[4];

      // Each LUT item contains four pixels (decodes as four raw bytes)
      rawPixel[0] = m_VGADCtrl->createRawPixel(m_context->paletteMap(0, 4));
      for (int i = 1; i < 4; i++) {
        rawPixel[i] = m_VGADCtrl->createRawPixel(m_context->paletteMap(i, 4));
      }
      // Each LUT item contains four pixels (decodes as four raw bytes)
      for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 4; j++) {
          int pixel = (i >> (6 - j * 2)) & 0b11;
          m_rawPixelLUT[(i * 4) + (j ^ 2)] = rawPixel[pixel];
        }
      }
      break;
    }

    case CGA_MODE_GFX_640x200_2COLORS:
    {
      const uint8_t bgColor = m_VGADCtrl->createRawPixel(m_context->paletteMap(0, 2)); // Background color always black
      const uint8_t fgColor = m_VGADCtrl->createRawPixel(m_context->paletteMap(1, 2));

      // Each LUT item contains eight pixels (decodes as eight raw bytes)
      for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 8; j++) {
          bool pixel = (i >> (7 - j)) & 1;
          m_rawPixelLUT[(i * 8) + (j ^ 2)] = (pixel == 0) ? bgColor : fgColor;
        }
      }
      break;
    }

    case TGA_MODE_GFX_160x200_16COLORS:
    {
      uint8_t rawPixel[16];

      // Convert palette indices to raw VGA pixels
      for (int i = 0; i < 16; i++) {
        rawPixel[i] = m_VGADCtrl->createRawPixel(m_context->paletteMap(i, 16));
      }

      for (int i = 0; i < 256; i++) {
          const uint8_t p = i & 0x0F;  // one logical pixel (low nibble)

#if 0
        m_rawPixelLUT[(i * 2) + 0] = rawPixel[p];
        m_rawPixelLUT[(i * 2) + 1] = rawPixel[p];
#else
        m_rawPixelLUT[(i * 4) + 0] = 0;           // dst[0]
        m_rawPixelLUT[(i * 4) + 1] = 0;           // dst[1]
        m_rawPixelLUT[(i * 4) + 2] = rawPixel[p]; // dst[2]
        m_rawPixelLUT[(i * 4) + 3] = rawPixel[p]; // dst[3]
#endif
      }
      break;
    }

    case TGA_MODE_GFX_320x200_16COLORS:
    {
      uint8_t rawPixel[16];

      for (int i = 0; i < 16; i++) {
        rawPixel[i] = m_VGADCtrl->createRawPixel(m_context->paletteMap(i, 16));
      }

      for (int i = 0; i < 256; i++) {
        const uint8_t p0 = (i >> 4) & 0x0F; // pixel 0 (high nibble)
        const uint8_t p1 =  i       & 0x0F; // pixel 1 (low nibble)

#if 0
        m_rawPixelLUT[(i * 2) + 0] = rawPixel[p0];
        m_rawPixelLUT[(i * 2) + 1] = rawPixel[p1];
#else
        // VGADirectController pixel byte order inside a dword:
        // dst[0] = pixel 2
        // dst[1] = pixel 3
        // dst[2] = pixel 0
        // dst[3] = pixel 1
        //
        // This LUT entry prepares only dst[2] and dst[3].
        // dst[0] and dst[1] will be filled by OR-ing with the next byte.

        m_rawPixelLUT[(i * 4) + 0] = 0;            // dst[0] placeholder
        m_rawPixelLUT[(i * 4) + 1] = 0;            // dst[1] placeholder
        m_rawPixelLUT[(i * 4) + 2] = rawPixel[p0]; // dst[2]
        m_rawPixelLUT[(i * 4) + 3] = rawPixel[p1]; // dst[3]
#endif
      }
      break;
    }

    case EGA_MODE_GFX_320x200_16COLORS:
    case EGA_MODE_GFX_640x200_16COLORS:

      // Initialize raw pixels LUT
      for (int i = 0; i < 16; i++) {
        // m_rawPixelLUT[i] = m_VGADCtrl->createRawPixel(EGA_palette[m_paletteMap[i]]);
        m_rawPixelLUT[i] = m_VGADCtrl->createRawPixel(m_context->paletteMap(i, 16));
      }

      // Initialize EGA LUT
      for (int p = 0; p < 4; p++) {
        for (int i = 0; i < 256; i++) {
          uint32_t val_H = 0;
          uint32_t val_L = 0;
          for (int bit = 0; bit < 8; bit++) {
            if (i & (0x80 >> bit)) {
              // Si el bit está activo, ponemos el peso del plano (1<<p) 
              // en el byte correspondiente al píxel
              if (bit < 4) {
                // Píxeles 0-3 van a la tabla HIGH
                val_H |= (uint32_t) (1 << p) << (((bit    )^2) * 8);
              } else {
                // Píxeles 4-7 van a la tabla LOW
                val_L |= (uint32_t) (1 << p) << (((bit - 4)^2) * 8);
              }
            }
          }
          m_egaLUT_H[p][i] = val_H;
          m_egaLUT_L[p][i] = val_L;
        }
      }
      break;

    case MDA_MODE_GFX_720x348_MONO:
    {
      // HGC/MDA 720x348 1bpp: expand each VRAM byte into 8 raw pixels using a 256-entry LUT.
      // Use same byte ordering trick as CGA (j ^ 2) to match VGADirectController internal packing.
      const uint8_t bgColor = m_VGADCtrl->createRawPixel(m_context->paletteMap(0, 2));
      const uint8_t fgColor = m_VGADCtrl->createRawPixel(m_context->paletteMap(1, 2));

      for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 8; j++) {
          const bool pixel = ((i >> (7 - j)) & 1) != 0;
          m_rawPixelLUT[(i * 8) + (j ^ 2)] = pixel ? fgColor : bgColor;
        }
      }
      break;
    }

    case EGA_MODE_GFX_640x350_16COLORS:
      break;

    case MCGA_MODE_GFX_320x200_256COLORS:
      // Initialize raw pixels LUT
      for (int i = 0; i < 256; i++) {
        m_rawPixelLUT[i] = m_VGADCtrl->createRawPixel(m_context->paletteMap(i, 0));
      }
      break;

    default:
      break;
  }
}

void VideoScanout::reallocFont(FontInfo const *font)
{
  releaseFont();
  m_font = *font;

  size_t size = 256 * ((m_font.width + 7) / 8) * m_font.height; // Size in bytes
  m_font.data = (uint8_t const *) heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

  // Copy font data into internal RAM
  memcpy((void *) m_font.data, font->data, size);
}

void VideoScanout::releaseFont()
{
  if (m_font.data) {
    heap_caps_free((void *) m_font.data);
    m_font.data = nullptr;
  }
}

void VideoScanout::updateCursorGlyph()
{
  int start = m_context->cursorStart();
  int end = m_context->cursorEnd();

  // readapt start->end to the actual font height to make sure the cursor is always visible
  if ((start <= end) && (end >= m_font.height)) {
    int h = end - start;
    end   = m_font.height - 1;
    start = end - h;
  }

  removeCursorGlyph();

  const int charBytes = (m_font.width + 7) / 8;   // Char width in bytes
  const int charSize = charBytes * m_font.height; // Char size in bytes

  if (!m_cursorGlyph) {
    m_cursorGlyph = (uint8_t *) heap_caps_malloc(charSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  }

  memset(m_cursorGlyph, 0, charSize);
  if ((end >= start) && (start >= 0) && (start < m_font.height) && (end < m_font.height)) {
    memset(m_cursorGlyph + (start * charBytes), 0xff, (end - start + 1) * charBytes);
  }
}

void VideoScanout::removeCursorGlyph()
{
  if (m_cursorGlyph) {
    heap_caps_free((void *) m_cursorGlyph);
    m_cursorGlyph = nullptr;
  }
}

void VideoScanout::setBorder(uint8_t color)
{
  m_rawBorderColor = m_VGADCtrl->createRawPixel(m_context->paletteMap(color, 16));
  printf("video: Set border color = %d\n", color);
}

void VideoScanout::showVolume(uint8_t volume)
{
  m_OSD_volumeLevel = volume;
  m_OSD_frame = m_frameCounter;
  m_OSD_showVolume = true;
}

uint8_t *VideoScanout::rawSnapshot(uint16_t *width, uint16_t *height)
{
  const size_t bufferSize = (size_t) m_width * m_height;

  // Allocate framebuffer in PSRAM
  uint8_t *framebuffer = (uint8_t *) heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!framebuffer) {
    printf("video: Unable to allocate framebuffer memory (for snapshot)\n");
    return nullptr;
  }

  // Render loop: call the SAME drawScanline callback used by VGADirectController
  for (int scanLine = 0; scanLine < m_height; scanLine += m_scanLines) {
    uint8_t *dst = framebuffer + scanLine * m_width;

    m_callback(this, dst, scanLine);
  }

  *width  = m_width;
  *height = m_height;
  return framebuffer;
}

void IRAM_ATTR VideoScanout::drawScanline_text_40x25(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 320;
  constexpr int textCols   = 40;
  constexpr int charWidth  = 8;
  constexpr int charHeight = 8;
  constexpr int charBytes  = (charWidth + 7) / 8;    // Char width in bytes
  constexpr int charSize   = charBytes * charHeight; // Char size in bytes
  constexpr int scanLines  = 4;

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;

    output->m_startAddress = adapter->startAddress();
    output->m_textPageSize = adapter->textPageSize();
    const uint8_t page = adapter->activePage();
    output->m_activePage = page;
    output->m_cursorRow = adapter->cursorRow(page);
    output->m_cursorCol = adapter->cursorCol(page);
    output->m_cursorEnabled = adapter->cursorEnabled();
    output->blinkEnabled = adapter->blinkEnabled();
  }

  // If the current scanline is outside the CGA active area,
  // fill the entire scanline with the border color.
  if ((scanLine < 20) || (scanLine >= 220)) {
    memset(dst, output->m_rawBorderColor, pixelsLine * scanLines);
    return;
  }

  const int activeScanLine = scanLine - 20;
  const int charScanline = scanLine & (charHeight - 1);
  const int textRow = scanLine / charHeight;

  uint8_t const *fontData = output->m_font.data + (charScanline * charBytes);

  // Note that in CGA video cards page base (m_activePage * m_textPageSize)
  // and m_startAddress are the SAME offset
  // const uint32_t pageBase = (uint32_t) output->m_activePage * output->m_textPageSize;
  const uint32_t base = output->m_startAddress << 1; // words to bytes

  uint8_t *src = output->m_vram + base + (textRow * textCols * 2);
  uint8_t *LUT = output->m_rawPixelLUT;

  bool showCursor = output->m_cursorEnabled && output->m_cursorRow == textRow && ((output->m_frameCounter & 0x1f) < 0xf);
  int cursorCol = output->m_cursorCol;

  bool bit7blink = output->blinkEnabled;
  bool blinktime = bit7blink && !((output->m_frameCounter & 0x3f) < 0x1f);

  for (int textCol = 0; textCol < textCols; textCol++) {

    int charIdx  = *src++;
    int charAttr = *src++;

    bool blink = false;
    if (bit7blink) {
      blink = blinktime && (charAttr & 0x80);
      charAttr &= 0x7f;
    }

    uint8_t bg = LUT[charAttr >> 4];
    uint8_t fg = blink ? bg : LUT[charAttr & 0xf];

    const uint8_t colors[2] = { bg, fg };

    uint8_t const *p_charBitmap = fontData + charIdx * charSize;

    auto p_dst = dst;

    if (showCursor && textCol == cursorCol) {

      uint8_t const *p_cursorBitmap = output->m_cursorGlyph + (charScanline * charBytes);

      for (int charRow = 0; charRow < scanLines; charRow++) {

        uint32_t charBitmap = *p_charBitmap | *p_cursorBitmap;

        *(p_dst + 0) = colors[(bool)(charBitmap & 0x20)];
        *(p_dst + 1) = colors[(bool)(charBitmap & 0x10)];
        *(p_dst + 2) = colors[(bool)(charBitmap & 0x80)];
        *(p_dst + 3) = colors[(bool)(charBitmap & 0x40)];
        *(p_dst + 4) = colors[(bool)(charBitmap & 0x02)];
        *(p_dst + 5) = colors[(bool)(charBitmap & 0x01)];
        *(p_dst + 6) = colors[(bool)(charBitmap & 0x08)];
        *(p_dst + 7) = colors[(bool)(charBitmap & 0x04)];

        p_dst += pixelsLine;
        p_charBitmap += charBytes;
        p_cursorBitmap += charBytes;
      }

    } else {

      for (int charRow = 0; charRow < scanLines; charRow++) {

        uint32_t charBitmap = *p_charBitmap;

        *(p_dst + 0) = colors[(bool)(charBitmap & 0x20)];
        *(p_dst + 1) = colors[(bool)(charBitmap & 0x10)];
        *(p_dst + 2) = colors[(bool)(charBitmap & 0x80)];
        *(p_dst + 3) = colors[(bool)(charBitmap & 0x40)];
        *(p_dst + 4) = colors[(bool)(charBitmap & 0x02)];
        *(p_dst + 5) = colors[(bool)(charBitmap & 0x01)];
        *(p_dst + 6) = colors[(bool)(charBitmap & 0x08)];
        *(p_dst + 7) = colors[(bool)(charBitmap & 0x04)];

        p_dst += pixelsLine;
        p_charBitmap += charBytes;
      }
    }
    dst += 8;
  }
}

void IRAM_ATTR VideoScanout::drawScanline_text_80x25(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 640;
  constexpr int textCols   = 80;
  constexpr int charWidth  = 8;
  constexpr int charHeight = 8;
  constexpr int charBytes  = (charWidth + 7) / 8;    // Char width in bytes
  constexpr int charSize   = charBytes * charHeight; // Char size in bytes
  constexpr int scanLines  = 4;

  auto output = (VideoScanout *) ctx;
/*
  if ((output->m_state == State::Paused) && xPortInIsrContext())
    return;
*/
  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;

    output->m_startAddress = adapter->startAddress();
    output->m_textPageSize = adapter->textPageSize();
    const uint8_t page = adapter->activePage();
    output->m_activePage = page;
    output->m_cursorRow = adapter->cursorRow(page);
    output->m_cursorCol = adapter->cursorCol(page);
    output->m_cursorEnabled = adapter->cursorEnabled();
    output->blinkEnabled = adapter->blinkEnabled();
  }

  // If the current scanline is outside the CGA active area,
  // fill the entire scanline with the border color.
  if ((scanLine < 20) || (scanLine >= 220)) {
    memset(dst, output->m_rawBorderColor, pixelsLine * scanLines);
    return;
  }

  const int activeScanLine = scanLine - 20;
  const int charScanline = activeScanLine & (charHeight - 1);
  const int textRow = activeScanLine / charHeight;

  uint8_t const *fontData = output->m_font.data + (charScanline * charBytes);

  // Note that in CGA video cards page base (m_activePage * m_textPageSize)
  // and m_startAddress are the SAME offset
  // const uint32_t pageBase = (uint32_t) output->m_activePage * output->m_textPageSize;
  uint32_t base = output->m_startAddress << 1; // words to bytes

  uint8_t *src = output->m_vram + base + (textRow * textCols * 2);
  uint8_t *LUT = output->m_rawPixelLUT;

  bool showCursor = output->m_cursorEnabled && output->m_cursorRow == textRow && ((output->m_frameCounter & 0x1f) < 0xf);
  int cursorCol = output->m_cursorCol;

  bool bit7blink = output->blinkEnabled;
  bool blinktime = bit7blink && !((output->m_frameCounter & 0x3f) < 0x1f);

  for (int textCol = 0; textCol < textCols; textCol++) {

    int charIdx  = *src++;
    int charAttr = *src++;

    bool blink = false;
    if (bit7blink) {
      blink = blinktime && (charAttr & 0x80);
      charAttr &= 0x7f;
    }

    uint8_t bg = LUT[charAttr >> 4];
    uint8_t fg = blink ? bg : LUT[charAttr & 0xf];

    //const uint8_t colors[2] = { bg, fg };
    uint8_t colors[2] = { bg, fg };

    uint8_t const *p_charBitmap = fontData + charIdx * charSize;

    auto p_dst = dst;

    if (showCursor && textCol == cursorCol) {

      uint8_t const *p_cursorBitmap = output->m_cursorGlyph + (charScanline * charBytes);

      if (charAttr == 0) { // Show always a cursor (when no attribute was defined)
      	colors[1] = blink ? bg : LUT[0x07];
	    }

      for (int charRow = 0; charRow < scanLines; charRow++) {

        const uint32_t charBitmap = *p_charBitmap | *p_cursorBitmap;

        *(p_dst + 0) = colors[(bool) (charBitmap & 0x20)];
        *(p_dst + 1) = colors[(bool) (charBitmap & 0x10)];
        *(p_dst + 2) = colors[(bool) (charBitmap & 0x80)];
        *(p_dst + 3) = colors[(bool) (charBitmap & 0x40)];
        *(p_dst + 4) = colors[(bool) (charBitmap & 0x02)];
        *(p_dst + 5) = colors[(bool) (charBitmap & 0x01)];
        *(p_dst + 6) = colors[(bool) (charBitmap & 0x08)];
        *(p_dst + 7) = colors[(bool) (charBitmap & 0x04)];

        p_dst += pixelsLine;
        p_charBitmap += charBytes;
        p_cursorBitmap += charBytes;
      }

    } else {

      for (int charRow = 0; charRow < scanLines; charRow++) {

        const uint32_t charBitmap = *p_charBitmap;

        *(p_dst + 0) = colors[(bool) (charBitmap & 0x20)];
        *(p_dst + 1) = colors[(bool) (charBitmap & 0x10)];
        *(p_dst + 2) = colors[(bool) (charBitmap & 0x80)];
        *(p_dst + 3) = colors[(bool) (charBitmap & 0x40)];
        *(p_dst + 4) = colors[(bool) (charBitmap & 0x02)];
        *(p_dst + 5) = colors[(bool) (charBitmap & 0x01)];
        *(p_dst + 6) = colors[(bool) (charBitmap & 0x08)];
        *(p_dst + 7) = colors[(bool) (charBitmap & 0x04)];

        p_dst += pixelsLine;
        p_charBitmap += charBytes;
      }
    }
    dst += 8;
  }

  if (output->m_OSD_showVolume) {
    // Hide after ~150 frames (~2.5 seconds @ 60Hz)
    if ((output->m_frameCounter - output->m_OSD_frame) > 150) {
      output->m_OSD_showVolume = false;
    } else {
      drawOSDVolume(output, pixelsLine, scanLines, charScanline, textRow, dst);
    }
  } else if (output->m_state == State::Paused) {
    // Show only when emulator is paused (you must set this flag from Computer/VideoSystem)
    drawOSDPause(output, pixelsLine, scanLines, charScanline, textRow, dst);
  }
}

void IRAM_ATTR VideoScanout::drawScanline_mda_80x25(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 640;
  constexpr int textCols   = 80;
  constexpr int charWidth  = 8;
  constexpr int charHeight = 16;
  constexpr int charBytes  = (charWidth + 7) / 8;    // Char width in bytes
  constexpr int charSize   = charBytes * charHeight; // Char size in bytes
  constexpr int scanLines  = 8;

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;

    output->m_startAddress = adapter->startAddress();
    output->m_textPageSize = adapter->textPageSize();
    const uint8_t page = adapter->activePage();
    output->m_activePage = page;
    output->m_cursorRow = adapter->cursorRow(page);
    output->m_cursorCol = adapter->cursorCol(page);
    output->m_cursorEnabled = adapter->cursorEnabled();
    output->blinkEnabled = adapter->blinkEnabled();
  }

  const int charScanline = scanLine & (charHeight - 1);
  const int textRow = scanLine / charHeight;

  uint8_t const *fontData = output->m_font.data + (charScanline * charBytes);

  // Note that in CGA video cards page base (m_activePage * m_textPageSize)
  // and m_startAddress are the SAME offset
  // const uint32_t pageBase = (uint32_t) output->m_activePage * output->m_textPageSize;
  uint32_t base = output->m_startAddress << 1; // words to bytes

  uint8_t *src = output->m_vram + base + (textRow * textCols * 2);
  uint8_t *LUT = output->m_rawPixelLUT;

  bool showCursor = output->m_cursorEnabled && output->m_cursorRow == textRow && ((output->m_frameCounter & 0x1f) < 0xf);
  int cursorCol = output->m_cursorCol;

  bool bit7blink = output->blinkEnabled;
  bool blinktime = bit7blink && !((output->m_frameCounter & 0x3f) < 0x1f);

  for (int textCol = 0; textCol < textCols; textCol++) {

    int charIdx  = *src++;
    int charAttr = *src++;

    bool blink = false;
    if (bit7blink) {
      blink = blinktime && (charAttr & 0x80);
      charAttr &= 0x7f;
    }

    //uint8_t bg = LUT[charAttr >> 4];
    //uint8_t fg = blink ? bg : LUT[charAttr & 0xf];
    uint8_t bg = LUT[0];
    uint8_t fg = blink ? bg : LUT[1];

    //const uint8_t colors[2] = { bg, fg };
    uint8_t colors[2] = { bg, fg };

    uint8_t const *p_charBitmap = fontData + charIdx * charSize;

    auto p_dst = dst;

    if (showCursor && textCol == cursorCol) {

      uint8_t const *p_cursorBitmap = output->m_cursorGlyph + (charScanline * charBytes);

      if (charAttr == 0) { // Show always a cursor (when no attribute was defined)
      	colors[1] = blink ? bg : LUT[0x07];
	    }

      for (int charRow = 0; charRow < scanLines; charRow++) {

        const uint32_t charBitmap = *p_charBitmap | *p_cursorBitmap;

        *(p_dst + 0) = colors[(bool) (charBitmap & 0x20)];
        *(p_dst + 1) = colors[(bool) (charBitmap & 0x10)];
        *(p_dst + 2) = colors[(bool) (charBitmap & 0x80)];
        *(p_dst + 3) = colors[(bool) (charBitmap & 0x40)];
        *(p_dst + 4) = colors[(bool) (charBitmap & 0x02)];
        *(p_dst + 5) = colors[(bool) (charBitmap & 0x01)];
        *(p_dst + 6) = colors[(bool) (charBitmap & 0x08)];
        *(p_dst + 7) = colors[(bool) (charBitmap & 0x04)];

        p_dst += pixelsLine;
        p_charBitmap += charBytes;
        p_cursorBitmap += charBytes;
      }

    } else {

      for (int charRow = 0; charRow < scanLines; charRow++) {

        const uint32_t charBitmap = *p_charBitmap;

        *(p_dst + 0) = colors[(bool) (charBitmap & 0x20)];
        *(p_dst + 1) = colors[(bool) (charBitmap & 0x10)];
        *(p_dst + 2) = colors[(bool) (charBitmap & 0x80)];
        *(p_dst + 3) = colors[(bool) (charBitmap & 0x40)];
        *(p_dst + 4) = colors[(bool) (charBitmap & 0x02)];
        *(p_dst + 5) = colors[(bool) (charBitmap & 0x01)];
        *(p_dst + 6) = colors[(bool) (charBitmap & 0x08)];
        *(p_dst + 7) = colors[(bool) (charBitmap & 0x04)];

        p_dst += pixelsLine;
        p_charBitmap += charBytes;
      }
    }
    dst += 8;
  }
}

void IRAM_ATTR VideoScanout::drawScanline_cga_320x200x4(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 320;                    // Pixels per scan line (m_width)
  constexpr int pixelsByte = 4;                      // Pixels per byte
  constexpr int bytesLine = pixelsLine / pixelsByte; // Bytes per line
  constexpr uint32_t vramMask = 0x3FFF;              // 16 KB = 0x3FFF

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;
    output->m_startAddress = adapter->startAddress();
  }

  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes

  // CGA planar banking:
  // - even scanlines go to bank 0 (offset 0x0000)
  // - odd scanlines go to bank 1 (offset 0x2000)
  const uint32_t bankOffset = (scanLine & 1) << 13; // 0x2000 if odd
  const uint32_t lineOffset = bytesLine * (scanLine >> 1);

  const uint32_t offset = (base + bankOffset + lineOffset) & vramMask;

  uint8_t *src = output->m_vram + offset;

  uint32_t *dst32 = (uint32_t *) dst;
  uint32_t *LUT32 = (uint32_t *) output->m_rawPixelLUT;

  for (int i = 0; i < pixelsLine; i += pixelsByte) {
    *dst32++ = LUT32[*src++];
  }
}

void IRAM_ATTR VideoScanout::drawScanline_cga_640x200x2(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 640;                    // Pixels per scanline (m_width)
  constexpr int pixelsByte = 8;                      // Pixels per byte
  constexpr int bytesLine = pixelsLine / pixelsByte; // Bytes per line
  constexpr uint32_t vramMask = 0x3FFF;              // 16 KB = 0x3FFF

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;
    output->m_startAddress = adapter->startAddress();
  }

  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes

  // CGA planar banking:
  // - even scanlines go to bank 0 (offset 0x0000)
  // - odd scanlines go to bank 1 (offset 0x2000)
  const uint32_t bankOffset = (scanLine & 1) << 13; // 0x2000 if odd
  const uint32_t lineOffset = bytesLine * (scanLine >> 1);

  const uint32_t offset = (base + bankOffset + lineOffset) & vramMask;

  uint8_t *src = output->m_vram + offset;

  uint64_t *dst64 = (uint64_t *) dst;
  uint64_t *LUT64 = (uint64_t *) output->m_rawPixelLUT;

  for (int i = 0; i < pixelsLine; i += pixelsByte) {
    *dst64++ = LUT64[*src++];
  }
}

void IRAM_ATTR VideoScanout::drawScanline_tandy_320x200x16(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 320;                    // Pixels per scan line (m_width)
  constexpr int pixelsByte = 2;                      // Pixels per byte
  constexpr int bytesLine = pixelsLine / pixelsByte; // Bytes per line
  constexpr uint32_t vramMask = 0x7FFF;              // 32 KB - 1 (Tandy VRAM size)

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;
    output->m_startAddress = adapter->startAddress();
  }

  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes

  // Tandy 4‑bank interleaving
  const uint32_t bank_order[] = {2, 3, 0, 1};
  const uint32_t bank = bank_order[scanLine & 3];
  const uint32_t row = scanLine >> 2;
  const uint32_t bankOffset = bank << 13;      // 8KB per bank (8192 bytes)
  const uint32_t lineOffset = row * bytesLine;

  const uint32_t offset = (base + bankOffset + lineOffset) & vramMask;

  uint8_t *src = output->m_vram + offset;

#if 0
  uint8_t *LUT = output->m_rawPixelLUT;

  for (int i = 0; i < bytesLine; i += 2) {
    const uint8_t b0 = *src++; // pixels 0 y 1
    const uint8_t b1 = *src++; // pixels 2 y 3

    dst[2] = LUT[b0 * 2 + 0];
    dst[3] = LUT[b0 * 2 + 1];
    dst[0] = LUT[b1 * 2 + 0];
    dst[1] = LUT[b1 * 2 + 1];
    dst += 4;
  }
#else
  // Cast the shared LUT to 32-bit entries locally
  const uint32_t *LUT32 = (const uint32_t *) output->m_rawPixelLUT;

  // Destination is naturally dword-aligned
  uint32_t *dst32 = (uint32_t *) dst;

  // Process 4 pixels (2 VRAM bytes) per iteration
  for (int i = 0; i < bytesLine; i += 2) {
    const uint8_t b0 = *src++; // pixels 0 and 1
    const uint8_t b1 = *src++; // pixels 2 and 3

    // Combine both LUT entries into a single dword:
    // b1 fills dst[0] and dst[1]
    // b0 fills dst[2] and dst[3]
    *dst32++ = (LUT32[b1] >> 16) | LUT32[b0];
  }
#endif
}

void IRAM_ATTR VideoScanout::drawScanline_tandy_640x200x4(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 640;                    // Pixels per scan line (m_width)
  constexpr int pixelsByte = 4;                      // Pixels per byte
  constexpr int bytesLine = pixelsLine / pixelsByte; // Bytes per line
  constexpr uint32_t vramMask = 0x7FFF;              // 32 KB - 1 (Tandy VRAM size)

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;
    output->m_startAddress = adapter->startAddress();
  }

  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes

  // Tandy 4‑bank interleaving
  const uint32_t bank_order[] = {2, 3, 0, 1};
  const uint32_t bank = bank_order[scanLine & 3];
  const uint32_t row = scanLine >> 2;
  const uint32_t bankOffset = bank << 13;      // 8KB per bank (8192 bytes)
  const uint32_t lineOffset = row * bytesLine;

  const uint32_t offset = (base + bankOffset + lineOffset) & vramMask;

  uint8_t *src = output->m_vram + offset;

  uint32_t *dst32 = (uint32_t *) dst;
  uint32_t *LUT32 = (uint32_t *) output->m_rawPixelLUT;

  for (int i = 0; i < pixelsLine; i += pixelsByte) {
    *dst32++ = LUT32[*src++];
  }
}

void IRAM_ATTR VideoScanout::drawScanline_ega_320x200x16(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 320;                    // Pixels per scan line (m_width)
  constexpr int pixelsByte = 8;                      // Pixels per byte
  constexpr int bytesLine = pixelsLine / pixelsByte; // Bytes per line

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    ScanoutContext *adapter = output->m_context;
  	output->m_startAddress = adapter->startAddress();
  	output->m_colorPlaneEnable = adapter->colorPlaneEnable();
  }

  const uint32_t vramMask = output->m_vramSize - 1;
  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes
  const uint32_t offset = (base + scanLine * bytesLine) & vramMask;

  const uint8_t *plane0 = output->m_plane[0] + offset;
  const uint8_t *plane1 = output->m_plane[1] + offset;
  const uint8_t *plane2 = output->m_plane[2] + offset;
  const uint8_t *plane3 = output->m_plane[3] + offset;

  // Cache plane enable mask
  const uint8_t colorPlaneEnable = output->m_colorPlaneEnable;

  const bool p0en = (colorPlaneEnable & 0x1) != 0;
  const bool p1en = (colorPlaneEnable & 0x2) != 0;
  const bool p2en = (colorPlaneEnable & 0x4) != 0;
  const bool p3en = (colorPlaneEnable & 0x8) != 0;

  // Cache LUTs
  const uint8_t *LUT = output->m_rawPixelLUT;

  const uint32_t *LUT_H0 = m_egaLUT_H[0];
  const uint32_t *LUT_H1 = m_egaLUT_H[1];
  const uint32_t *LUT_H2 = m_egaLUT_H[2];
  const uint32_t *LUT_H3 = m_egaLUT_H[3];

  const uint32_t *LUT_L0 = m_egaLUT_L[0];
  const uint32_t *LUT_L1 = m_egaLUT_L[1];
  const uint32_t *LUT_L2 = m_egaLUT_L[2];
  const uint32_t *LUT_L3 = m_egaLUT_L[3];

  // Main scanline loop
  for (int i = 0; i < bytesLine; i++) {
  	const uint8_t p0 = p0en ? plane0[i] : 0;
    const uint8_t p1 = p1en ? plane1[i] : 0;
    const uint8_t p2 = p2en ? plane2[i] : 0;
    const uint8_t p3 = p3en ? plane3[i] : 0;

    const uint32_t pixels_H = LUT_H0[p0] | LUT_H1[p1] | LUT_H2[p2] | LUT_H3[p3];
    const uint32_t pixels_L = LUT_L0[p0] | LUT_L1[p1] | LUT_L2[p2] | LUT_L3[p3];

    const uint8_t *pixel_H = (const uint8_t *) &pixels_H;
    const uint8_t *pixel_L = (const uint8_t *) &pixels_L;

    *dst++ = LUT[pixel_H[0]];
    *dst++ = LUT[pixel_H[1]];
    *dst++ = LUT[pixel_H[2]];
    *dst++ = LUT[pixel_H[3]];
    *dst++ = LUT[pixel_L[0]];
    *dst++ = LUT[pixel_L[1]];
    *dst++ = LUT[pixel_L[2]];
    *dst++ = LUT[pixel_L[3]];
  }
}

void IRAM_ATTR VideoScanout::drawScanline_ega_640x200x16(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 640;                    // Pixels per scan line (m_width)
  constexpr int pixelsByte = 8;                      // Pixels per byte
  constexpr int bytesLine = pixelsLine / pixelsByte; // Bytes per line

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    ScanoutContext *adapter = output->m_context;
  	output->m_startAddress = adapter->startAddress();
  	output->m_colorPlaneEnable = adapter->colorPlaneEnable();
  }

  const uint32_t vramMask = output->m_vramSize - 1;
  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes
  const uint32_t offset = (base + scanLine * bytesLine) & vramMask;

  // Cache planes locally (register-friendly
  const uint8_t *plane0 = output->m_plane[0] + offset;
  const uint8_t *plane1 = output->m_plane[1] + offset;
  const uint8_t *plane2 = output->m_plane[2] + offset;
  const uint8_t *plane3 = output->m_plane[3] + offset;

  // Cache plane enable mask
  const uint8_t colorPlaneEnable = output->m_colorPlaneEnable;

  const bool p0en = (colorPlaneEnable & 0x1) != 0;
  const bool p1en = (colorPlaneEnable & 0x2) != 0;
  const bool p2en = (colorPlaneEnable & 0x4) != 0;
  const bool p3en = (colorPlaneEnable & 0x8) != 0;

  // Cache LUTs
  const uint8_t *LUT = output->m_rawPixelLUT;

  const uint32_t *LUT_H0 = m_egaLUT_H[0];
  const uint32_t *LUT_H1 = m_egaLUT_H[1];
  const uint32_t *LUT_H2 = m_egaLUT_H[2];
  const uint32_t *LUT_H3 = m_egaLUT_H[3];

  const uint32_t *LUT_L0 = m_egaLUT_L[0];
  const uint32_t *LUT_L1 = m_egaLUT_L[1];
  const uint32_t *LUT_L2 = m_egaLUT_L[2];
  const uint32_t *LUT_L3 = m_egaLUT_L[3];

  // Main scanline loop
  for (int i = 0; i < bytesLine; i++) {
  	const uint8_t p0 = p0en ? plane0[i] : 0;
    const uint8_t p1 = p1en ? plane1[i] : 0;
    const uint8_t p2 = p2en ? plane2[i] : 0;
    const uint8_t p3 = p3en ? plane3[i] : 0;

    const uint32_t pixels_H = LUT_H0[p0] | LUT_H1[p1] | LUT_H2[p2] | LUT_H3[p3];
    const uint32_t pixels_L = LUT_L0[p0] | LUT_L1[p1] | LUT_L2[p2] | LUT_L3[p3];

    const uint8_t *pixel_H = (const uint8_t *) &pixels_H;
    const uint8_t *pixel_L = (const uint8_t *) &pixels_L;

    *dst++ = LUT[pixel_H[0]];
    *dst++ = LUT[pixel_H[1]];
    *dst++ = LUT[pixel_H[2]];
    *dst++ = LUT[pixel_H[3]];
    *dst++ = LUT[pixel_L[0]];
    *dst++ = LUT[pixel_L[1]];
    *dst++ = LUT[pixel_L[2]];
    *dst++ = LUT[pixel_L[3]];
  }
}

void IRAM_ATTR VideoScanout::drawScanline_mda_720x348x2(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 720;            // Pixels per scan line (m_width)
  constexpr int bytesLine  = pixelsLine / 8; // Bytes per line
  constexpr int bankSize   = 0x2000;
  constexpr uint32_t vramMask = 0xFFFF;      // 64KB - 1

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;
    output->m_startAddress = adapter->startAddress();
  }

  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes

  // Hercules graphics interleaving: 4 banks of 0x2000 bytes, selected by (y & 3)
  const uint32_t bank = (uint32_t) scanLine & 3;
  const uint32_t row  = (uint32_t) scanLine >> 2;
  const uint32_t offset = (base + bank * bankSize + row * bytesLine) & vramMask;

  const uint8_t *src = output->m_vram + offset;

  uint64_t *dst64 = (uint64_t *) dst;
  uint64_t *LUT64 = (uint64_t *) output->m_rawPixelLUT;

  for (int i = 0; i < bytesLine; ++i) {
    *dst64++ = LUT64[*src++];
  }
}

void IRAM_ATTR VideoScanout::drawScanline_ega_640x350x16(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 640;                    // Pixels per scan line (m_width)
  constexpr int pixelsByte = 8;                      // Pixels per byte
  constexpr int bytesLine = pixelsLine / pixelsByte; // Bytes per line

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    ScanoutContext *adapter = output->m_context;
  	output->m_startAddress = adapter->startAddress();
  	output->m_colorPlaneEnable = adapter->colorPlaneEnable();
  }

  const uint32_t vramMask = output->m_vramSize - 1;
  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes
  const uint32_t offset = (base + scanLine * bytesLine) & vramMask;

  // Cache planes locally (register-friendly
  const uint8_t *plane0 = output->m_plane[0] + offset;
  const uint8_t *plane1 = output->m_plane[1] + offset;
  const uint8_t *plane2 = output->m_plane[2] + offset;
  const uint8_t *plane3 = output->m_plane[3] + offset;

  // Cache plane enable mask
  const uint8_t colorPlaneEnable = output->m_colorPlaneEnable;

  const bool p0en = (colorPlaneEnable & 0x1) != 0;
  const bool p1en = (colorPlaneEnable & 0x2) != 0;
  const bool p2en = (colorPlaneEnable & 0x4) != 0;
  const bool p3en = (colorPlaneEnable & 0x8) != 0;

  // Cache LUTs
  const uint8_t *LUT = output->m_rawPixelLUT;

  const uint32_t *LUT_H0 = m_egaLUT_H[0];
  const uint32_t *LUT_H1 = m_egaLUT_H[1];
  const uint32_t *LUT_H2 = m_egaLUT_H[2];
  const uint32_t *LUT_H3 = m_egaLUT_H[3];

  const uint32_t *LUT_L0 = m_egaLUT_L[0];
  const uint32_t *LUT_L1 = m_egaLUT_L[1];
  const uint32_t *LUT_L2 = m_egaLUT_L[2];
  const uint32_t *LUT_L3 = m_egaLUT_L[3];

  for (int i = 0; i < bytesLine; i++) {
  	const uint8_t p0 = p0en ? plane0[i] : 0;
    const uint8_t p1 = p1en ? plane1[i] : 0;
    const uint8_t p2 = p2en ? plane2[i] : 0;
    const uint8_t p3 = p3en ? plane3[i] : 0;

    const uint32_t pixels_H = LUT_H0[p0] | LUT_H1[p1] | LUT_H2[p2] | LUT_H3[p3];
    const uint32_t pixels_L = LUT_L0[p0] | LUT_L1[p1] | LUT_L2[p2] | LUT_L3[p3];

    const uint8_t *pixel_H = (const uint8_t *) &pixels_H;
    const uint8_t *pixel_L = (const uint8_t *) &pixels_L;

    *dst++ = LUT[pixel_H[0]];
    *dst++ = LUT[pixel_H[1]];
    *dst++ = LUT[pixel_H[2]];
    *dst++ = LUT[pixel_H[3]];
    *dst++ = LUT[pixel_L[0]];
    *dst++ = LUT[pixel_L[1]];
    *dst++ = LUT[pixel_L[2]];
    *dst++ = LUT[pixel_L[3]];
  }
}

void IRAM_ATTR VideoScanout::drawScanline_mcga_320x200x256(void *ctx, uint8_t *dst, int scanLine)
{
  constexpr int pixelsLine = 320;       // Pixels per scan line (m_width)
  constexpr uint32_t vramMask = 0xFFFF; // 64KB - 1

  auto output = (VideoScanout *) ctx;

  if ((scanLine == 0) && (output->m_state == State::Running)) {
    output->m_frameCounter++;

    // Video adapter context
    ScanoutContext *adapter = output->m_context;
    output->m_startAddress = adapter->startAddress();
  }

  const uint32_t base = (output->m_startAddress << 1) & vramMask; // words to bytes
  const uint32_t offset = (base + pixelsLine * scanLine) & vramMask;

  uint8_t *src = output->m_vram + offset;
  uint8_t *LUT = output->m_rawPixelLUT;

#if 0
  for (int i = 0; i < pixelsLine; i += 4) {
    *(dst + 0) = LUT[*(src + 2)];
    *(dst + 1) = LUT[*(src + 3)];
    *(dst + 2) = LUT[*(src + 0)];
    *(dst + 3) = LUT[*(src + 1)];
    dst += 4;
    src += 4;
  }
#else
  for (int i = 0; i < pixelsLine; i += 16) {
    // Load four 32-bit words (16 bytes total) from VRAM
    uint32_t v0 = *(uint32_t*)(src + 0);
    uint32_t v1 = *(uint32_t*)(src + 4);
    uint32_t v2 = *(uint32_t*)(src + 8);
    uint32_t v3 = *(uint32_t*)(src + 12);

    uint8_t *d = dst + i;

    // Reordering pattern required by VGADirectController internal byte packing
    d[0] = LUT[(v0 >> 16) & 0xFF];
    d[1] = LUT[(v0 >> 24) & 0xFF];
    d[2] = LUT[(v0 >>  0) & 0xFF];
    d[3] = LUT[(v0 >>  8) & 0xFF];

    d[4] = LUT[(v1 >> 16) & 0xFF];
    d[5] = LUT[(v1 >> 24) & 0xFF];
    d[6] = LUT[(v1 >>  0) & 0xFF];
    d[7] = LUT[(v1 >>  8) & 0xFF];

    d[8] = LUT[(v2 >> 16) & 0xFF];
    d[9] = LUT[(v2 >> 24) & 0xFF];
    d[10]= LUT[(v2 >>  0) & 0xFF];
    d[11]= LUT[(v2 >>  8) & 0xFF];

    d[12]= LUT[(v3 >> 16) & 0xFF];
    d[13]= LUT[(v3 >> 24) & 0xFF];
    d[14]= LUT[(v3 >>  0) & 0xFF];
    d[15]= LUT[(v3 >>  8) & 0xFF];

    src += 16;
  }
#endif
}

inline __attribute__((always_inline))
void drawOSDVolume(VideoScanout *output, int pixelsLine, int scanLines, int charScanline, int textRow, uint8_t *dst)
{
  constexpr int charWidth  = 8;
  constexpr int charHeight = 8;
  constexpr int charBytes  = (charWidth + 7) / 8;    // Char width in bytes
  constexpr int charSize   = charBytes * charHeight; // Char size in bytes

  // Only affect first 2 text rows: row 0 = label, row 1 = meter
  if (textRow > 1)
    return;

  // Glyph is 8 px high; this callback draws `scanLines` (=4) physical scanlines.
  // We only draw while we are inside the glyph height.
  if (charScanline >= 8)
    return;

  // IMPORTANT:
  // The main renderer advanced `dst` by (textCols * charWidth) = (80 * 8) = 640 bytes,
  // which equals pixelsLine. Recover the base pointer for this scanline block.
  uint8_t *dstBase = dst - pixelsLine;

  constexpr int lineWidth       = 4; // stripe thickness in pixels
  constexpr int totalColorLines = 16;
  constexpr int totalLines = 2 * totalColorLines - 1;

  // Right margin = 2 characters = 16 pixels
  constexpr int rightMargin   = 16;

  // Label: "VOLUME"
  constexpr int textLen         = 6;
  constexpr int textWidth     = textLen * charWidth;   // 6 * 8 = 48

  // Meter width in pixels
  constexpr int meterWidth    = totalLines * lineWidth;

  // Align both rows to the same right-aligned block
  constexpr int blockWidth    = (textWidth > meterWidth) ? textWidth : meterWidth;

  // Block start X (right-aligned with margin)
  const int osdStartX = (pixelsLine - rightMargin - blockWidth) & ~7;

  const uint8_t bg  = output->m_OSD_rawPixelBg;  // background (black)
  const uint8_t fgH = output->m_OSD_rawPixelFgH; // bright foreground (yellow)
  const uint8_t fgL = output->m_OSD_rawPixelFgL; // light foreground (light gray)

  if (textRow == 0) { // Row 0: Draw "VOLUME"

    // Bit-to-pixel mapping used by your main renderer
    constexpr uint8_t bitMask[8] = { 0x20, 0x10, 0x80, 0x40, 0x02, 0x01, 0x08, 0x04 };

    const uint8_t text[textLen] = {
      (uint8_t) 'V', (uint8_t) 'O', (uint8_t) 'L', (uint8_t) 'U', (uint8_t) 'M', (uint8_t) 'E'
    };

    // Precompute glyph base pointers (no STL)
    const uint8_t *glyph[textLen];
    for (int c = 0; c < textLen; c++)
      glyph[c] = output->m_font.data + (int) text[c] * charSize;

    // Draw the `scanLines` physical scanlines of this callback
    for (int sl = 0; sl < scanLines; sl++) {
      const int glyphRow = charScanline + sl;  // 0..7
      uint8_t *line = dstBase + sl * pixelsLine;

      // Draw each character (8 px wide)
      for (int c = 0; c < textLen; c++) {
        const uint8_t bits = glyph[c][glyphRow * charBytes];
        uint8_t *p = line + osdStartX + c * charWidth;

        for (int px = 0; px < 8; px++) {
          p[px] = (bits & bitMask[px]) ? fgH : bg;
        }
      }

      // If meter is wider than text, clear the rest of the block to black
      if (meterWidth > textWidth) {
        uint8_t *p = line + osdStartX + textWidth;
        const int fill = meterWidth - textWidth;
        for (int i = 0; i < fill; i++) {
          p[i] = bg;
        }
      }
    }
  } else if (textRow == 1) { // --- ROW 1: Draw stripes meter ---

    const int activeLines = (output->m_OSD_volumeLevel * totalColorLines) / 127;

    for (int sl = 0; sl < scanLines; sl++) {
      uint8_t *row = dstBase + sl * pixelsLine;

      int x = osdStartX;
      int lineIndex = 0; // Colored line index
      for (int i = 0; i < totalLines; i++) {
        uint8_t color;

        if ((i & 1) == 0) {
          // Color stripe: yellow for active, gray for remaining
          color = (lineIndex < activeLines) ? fgH  : fgL;
          lineIndex++;
        } else {
          color = bg; // Black stripe separator
        }

        for (int w = 0; w < lineWidth; w++) {
          row[(x + w) ^ 2] = color;
        }
        x += lineWidth;
      }
    }
  }
}

inline __attribute__((always_inline))
void drawOSDPause(VideoScanout *output, int pixelsLine, int scanLines, int charScanline, int textRow, uint8_t *dst)
{
  constexpr int charWidth  = 8;
  constexpr int charHeight = 8;
  constexpr int charBytes  = (charWidth + 7) / 8;    // Char width in bytes
  constexpr int charSize   = charBytes * charHeight; // Char size in bytes

  // Only first text row
  if (textRow != 0)
    return;

  // Only within glyph height (8px)
  if (charScanline >= 8)
    return;

  // Recover base pointer for this scanline block (main loop advanced dst by pixelsLine)
  uint8_t *dstBase = dst - pixelsLine;

  // Layout constants
  constexpr int rightMargin = 16;                // 2 characters margin (in pixels)
  constexpr int textLen = 5;                     // "PAUSE"
  constexpr int textWidth = textLen * charWidth; // 5 * 8 = 40 (in pixels)
  constexpr int gap = charWidth;                 // one character gap (8px)
  
  constexpr int pauseBarWidth = 4;
  constexpr int pauseBarGap = 4;

  constexpr int iconWidth = (pauseBarWidth * 2) + pauseBarGap;
  constexpr int osdWidth = textWidth + gap + iconWidth;

  // Right-aligned start position
  const int osdStartX = (pixelsLine - rightMargin - osdWidth) & ~7;

  // Safety guard
  if (osdStartX < 0)
    return;

  // Colors
  const uint8_t fg = output->m_OSD_rawPixelFgH; // foreground (use same as volume/yellow)
  const uint8_t bg = output->m_OSD_rawPixelBg;  // background (black)

  // Bit-to-pixel mapping used by your main text renderer:
  // p[0]=0x20, p[1]=0x10, p[2]=0x80, p[3]=0x40, p[4]=0x02, p[5]=0x01, p[6]=0x08, p[7]=0x04
  constexpr uint8_t bitMask[8] = { 0x20, 0x10, 0x80, 0x40, 0x02, 0x01, 0x08, 0x04 };

  // Text to draw
  const uint8_t text[textLen] = {
    (uint8_t)'P', (uint8_t)'A', (uint8_t)'U', (uint8_t)'S', (uint8_t)'E'
  };

  // Precompute glyph pointers
  const uint8_t *glyph[textLen];
  for (int c = 0; c < textLen; ++c)
    glyph[c] = output->m_font.data + (int)text[c] * charSize;

  // Draw the 4 physical scanlines of this callback
  for (int sl = 0; sl < scanLines; ++sl) {

    const int glyphRow = charScanline + sl;   // 0..7
    uint8_t *line = dstBase + sl * pixelsLine;

    // Draw "PAUSE"
    for (int c = 0; c < textLen; ++c) {

      const uint8_t bits = glyph[c][glyphRow * charBytes];
      uint8_t *p = line + osdStartX + c * charWidth;

      for (int px = 0; px < 8; ++px)
        p[px] = (bits & bitMask[px]) ? fg : bg;
    }

    // Draw gap (black)
    uint8_t *p = line + osdStartX + textWidth;
    for (int i = 0; i < gap; ++i)
      p[i] = bg;

    // Draw "||" icon
    const int iconX = osdStartX + textWidth + gap;

    // Clear icon area
    for (int i = 0; i < iconWidth; ++i)
      line[(iconX + i) ^ 2] = bg;

    // Left bar: starts at 0
    for (int i = 0; i < pauseBarWidth; ++i)
      line[(iconX + i) ^ 2] = fg;

    // Right bar: starts after gap
    const int rightBarX = iconX + pauseBarWidth + pauseBarGap;
    for (int i = 0; i < pauseBarWidth; ++i)
      line[(rightBarX + i) ^ 2] = fg;
  }
}

} // end of namespace
