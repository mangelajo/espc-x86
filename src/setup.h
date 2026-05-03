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

#include <stdint.h>

// Return values
#define SETUP_OK    0
#define SETUP_ERROR 1

#ifdef NATIVE_BUILD
  // On native, use a local directory instead of /sdcard (no SD hardware)
  #define SD_MOUNT_PATH  ".sd_card"
#else
  #define SD_MOUNT_PATH  "/sdcard"
#endif
#define FAT_MOUNT_PATH "/fat"

#define MAX_FILEPATH_LEN 256

// Path to emulator configuration file
#define SETUP_BASE_PATH           SD_MOUNT_PATH "/espc-x86"
#define SETUP_CONFIG_FILEPATH     SETUP_BASE_PATH "/setup.cfg"

// Default paths
#define SETUP_DEFAULT_MEDIA_PATH  SETUP_BASE_PATH "/media"
#define SETUP_DEFAULT_DISKS_PATH  SETUP_BASE_PATH "/disks"
#define SETUP_SNAPHOTS_PATH       SETUP_BASE_PATH "/snapshots"


// Default values
#define SETUP_DEFAULT_VIDEO     0   // CGA
#define SETUP_DEFAULT_SOUND     0   // none
#define SETUP_DEFAULT_SPEAKER   1
#define SETUP_DEFAULT_JOYSTICK  0

#define SETUP_DEFAULT_RAM       640 // KB
#define SETUP_DEFAULT_KEYBOARD  0   // XT
#define SETUP_DEFAULT_KEYMAP    "US"
#define SETUP_DEFAULT_TURBO     0
#define SETUP_DEFAULT_MOUSE     0

#define SETUP_DEFAULT_BOOT      2   // C:

// Emulator setup structure
struct Setup {
  uint8_t  video;            // VideoAdapterType
  uint8_t  sound;            // 0 = none, 1 = adlib
  bool     speaker;          // PC speaker enabled
  uint8_t  joystick;         // 0 = none, 1 = game port

  uint16_t ram;              // RAM size in KB
  uint8_t  keyboard;         // 0 = XT, 1 = AT
  char     keymap[8];        // "US", "UK", "ES"
  bool     turbo;            // Turbo mode
  bool     mouse;            // Mouse enabled

  char     media_path[128];  // Path to disk images
  char     disks_path[128];  // Path to ZIP files

  char     drive[4][128];    // A:..D: (filenames or relative paths)
  uint8_t  boot;             // Boot drive index (0..3)
};

// Load configuration
int setupLoad(Setup *cfg, const char *filepath = SETUP_CONFIG_FILEPATH);

// Save configuration
int setupSave(const Setup *cfg, const char *filepath = SETUP_CONFIG_FILEPATH);
