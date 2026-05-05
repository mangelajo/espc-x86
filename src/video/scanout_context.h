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

#include "drivers/video/display.h"

#include <stdint.h>

using fabgl::RGB222;

namespace video {

// Read-only interface providing the video state consumed
// by the video scanout engine
class ScanoutContext {

public:

  virtual bool isPlanar() const { return false; }

  // Linear (CGA)
  virtual uint8_t *vram() { return nullptr; }

  // Planar (EGA)
  virtual uint8_t *plane(uint8_t index) { return nullptr; }

  // Total VRAM size in bytes, used for address wrapping
  virtual uint32_t vramSize() = 0;

  // CRTC start address, expressed in words
  virtual uint32_t startAddress() = 0;
  virtual uint16_t lineOffset() { return 0; } // only for EGA
  virtual uint16_t textPageSize() = 0;

  virtual bool cursorEnabled() = 0;
  virtual uint8_t cursorStart() = 0;
  virtual uint8_t cursorEnd() = 0;

  virtual uint8_t activePage() = 0;
  virtual uint8_t cursorRow(uint8_t page) = 0;
  virtual uint8_t cursorCol(uint8_t page) = 0;

  // Maps a logical color index to a physical RGB value
  virtual RGB222 paletteMap(uint8_t index, uint8_t total) = 0;

  virtual bool blinkEnabled() = 0;

  virtual uint8_t colorPlaneEnable() = 0;

  // Incremented whenever scanout-relevant state changes
  virtual uint32_t renderStamp() = 0;
};

} // end of namespace
