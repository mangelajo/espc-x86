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
#include "video/video_scanout.h"

#include <stdint.h>

namespace video {

class DummyVideoCard;

// Type of hardware video adapter currently emulated
enum class VideoAdapterType : uint8_t {
  CGA,
  EGA,
  HGC,
  TGA,
  MCGA
};

// VideoSystem is the interface that binds together:
//  - one active VideoAdapter (CGA/EGA/HGC)
//  - one VideoScanout (always alive)
//  - an optional temporary DummyVideoCard (menu / OSD)
class VideoSystem {

public:

   VideoSystem();
  ~VideoSystem();

  // Initializes the scanout engine and stores a pointer to system RAM
  void init(uint8_t *ram);

  // Sets the active hardware adapter (CGA, EGA or HGC).
  // The adapter instance is NOT destroyed when switching to Dummy mode.
  void active(VideoAdapterType type,
              VideoAdapter *adapter,
              ScanoutContext *context);

  // Returns the active hardware adapter (used by CPU for INT10h, ports, VRAM)
  VideoAdapter *adapter() const { return m_adapter; }

  // Suspends the current video adapter and switches scanout to a Dummy context.
  // This is a non-nestable operation.
  // Returns the Dummy context so the caller can access its VRAM (e.g. for OSD).
  ScanoutContext *suspend(int mode = CGA_MODE_TEXT_80x25_16COLORS);

  // Resumes normal emulation by restoring the previous scanout context and mode
  // The Dummy is destroyed and its memory released.
  bool resume();

  // Returns true if the system is currently suspended (Dummy active)
  bool suspended() const { return m_dummy != nullptr; }

  void pause(bool enable) { m_scanout.pause(enable); }
  void showVolume(uint8_t vol) { m_scanout.showVolume(vol); }

  uint8_t *rawSnapshot(uint16_t *width, uint16_t *height) { return m_scanout.rawSnapshot(width, height); }

private:

  // Applies scanout source and mode in a controlled order
  void setScanout(ScanoutContext *context, uint8_t mode);

  uint8_t          *m_ram;
  VideoScanout      m_scanout;

  VideoAdapterType  m_type;
  VideoAdapter     *m_adapter; // Active CGA/EGA/HGC
  ScanoutContext   *m_context; // Context published by the active adapter

  // Saved state while suspended
  ScanoutContext   *m_prevContext;
  uint8_t           m_prevMode;

  // Temporary Dummy (non-nestable)
  DummyVideoCard   *m_dummy;
};

} // namespace fabgl
