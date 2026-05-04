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

#include "host/settings.h"
#include "host/osd.h"
#include "host/lang.h"

#include "host/sdcard.h"
#include "host/vfs_fat.h"
#include "host/unzip.h"

#include "setup.h"

#include "esp_system.h"

#include <stdio.h>
#include <string.h>

static Setup cfg;

Settings::Settings(Computer *computer) :
  m_computer(computer),
  m_osd(nullptr)
{
}

void Settings::show()
{
  static char dir[MAX_FILEPATH_LEN];
  static char filename[MAX_FILEPATH_LEN];

  printf("osd: init...\n");

  auto ctx = m_computer->video_suspend(CGA_MODE_TEXT_80x25_16COLORS);

  init(ctx->vram());

  setupLoad(&cfg);

  m_osd->frame(0, 0, 80, 25, Lang::get(Lang::Msg::MenuTitle),
               true, COL_WHITE, COL_BLUE, false, false);

  m_osd->frame(8, 5, 64, 13, Lang::get(Lang::Msg::MenuSystemTitle),
               false, COL_WHITE, COL_CYAN, true, true);

  m_osd->text(33, 7, Lang::get(Lang::Msg::MenuSystemDescriptions),
              COL_WHITE, COL_CYAN);

  int opt = m_osd->menu(10, 7, 20, Lang::get(Lang::Msg::MenuSystemOptions), COL_CYAN);

  switch (opt) {

    case 0:
      esp_restart();
      break;

    case 1:
      m_computer->reboot();
      break;

    case 2:
    {
      int ret;
      int video = (int) cfg.video;
      int sound = (int) cfg.sound;
#if 0
      m_osd->frame(24, 4, 31, 17, Lang::get(Lang::Msg::MenuConfigTitle),
                   false, COL_WHITE, COL_LIGHTGRAY, true, true);
      m_osd->frame(26, 6, 27, 8, Lang::get(Lang::Msg::MenuConfigVideoTitle),
                   false, COL_WHITE, COL_LIGHTGRAY, true, false);
      ret = m_osd->radioList(28, 8, Lang::get(Lang::Msg::MenuConfigVideoOptions),
                             0, COL_LIGHTGRAY);
      m_osd->frame(26, 14, 27, 6, Lang::get(Lang::Msg::MenuConfigSoundTitle),
                   false, COL_WHITE, COL_LIGHTGRAY, true, false);
      ret = m_osd->radioList(28, 16, Lang::get(Lang::Msg::MenuConfigSoundOptions),
                             0, COL_LIGHTGRAY);
#else
      m_osd->frame(24, 3, 31, 18, Lang::get(Lang::Msg::MenuConfigTitle),
                   false, COL_WHITE, COL_LIGHTGRAY, true, true);
      m_osd->frame(26, 5, 27, 9, Lang::get(Lang::Msg::MenuConfigVideoTitle),
                   false, COL_WHITE, COL_LIGHTGRAY, true, false);
      m_osd->frame(26, 14, 27, 6, Lang::get(Lang::Msg::MenuConfigSoundTitle),
                   false, COL_WHITE, COL_LIGHTGRAY, true, false);

      m_osd->radioGroupBegin();
      m_osd->radioGroupAdd(28, 7, Lang::get(Lang::Msg::MenuConfigVideoOptions), &video);
      m_osd->radioGroupAdd(28, 16, Lang::get(Lang::Msg::MenuConfigSoundOptions), &sound);
      ret = m_osd->radioGroupRun(COL_LIGHTGRAY);
      if (ret != -1) {
        m_osd->frame(22, 8, 34, 7, Lang::get(Lang::Msg::MenuConfigTitle),
                    true, COL_WHITE, COL_RED, true, true);

        m_osd->text(25, 10, Lang::get(Lang::Msg::MsgSaveAndReset),
                    COL_WHITE, COL_RED);

        ret = m_osd->menuBar(31, 12, 0, "Ok;Cancel", COL_RED);
        if (ret == 0) {
          cfg.video = (uint8_t) video;
          cfg.sound = (uint8_t) sound;
          setupSave(&cfg);
          esp_restart();
        }
      }
#endif
      break;
    }

    case 3:
    {
      m_osd->frame(22, 8, 35, 10, Lang::get(Lang::Msg::MenuSystemTitle),
                   false, COL_WHITE, COL_LIGHTGRAY, true, true);

      m_osd->frame(24, 10, 16, 4, "Floppy",
                   false, COL_YELLOW, COL_LIGHTGRAY, true, false);

      m_osd->text(26, 11, "3.5\" HD\n1440 KB",
                  COL_WHITE, COL_LIGHTGRAY);

      m_osd->frame(40, 10, 16, 4, "Hard disk",
                   false, COL_YELLOW, COL_LIGHTGRAY, true, false);

      m_osd->text(42, 11, "8 MB HDD", COL_WHITE, COL_LIGHTGRAY);

      int index = m_osd->menuBar(25, 15, 0, Lang::get(Lang::Msg::MenuDriveOptions), COL_LIGHTGRAY);
      if (index != -1) {

        m_osd->frame(4, 4, 72, 18, Lang::get(Lang::Msg::MenuDiskImage),
                     false, COL_WHITE, COL_LIGHTGRAY, true, true);

        sprintf(dir, "%s/%s", SD_MOUNT_PATH, cfg.disks_path);
        int res = m_osd->fileBrowser(5, 6, 70, 15, dir, filename);
        if (res != -1) {
          mount_disk_image(index, filename);
        }
      }
      break;
    }

    default:
      break;
  }

  shutdown();
  m_computer->video_resume();

  printf("osd: finished\n");
}

void Settings::mountFloppy()
{
  static char dir[MAX_FILEPATH_LEN];
  static char filename[MAX_FILEPATH_LEN];

  printf("osd: Mount floppy... in\n");
  auto ctx = m_computer->video_suspend(CGA_MODE_TEXT_80x25_16COLORS);

  init(ctx->vram());

  m_osd->frame(0, 0, 80, 25, Lang::get(Lang::Msg::MenuTitle),
               true, COL_WHITE, COL_BLUE, false, false);

  m_osd->frame(22, 8, 35, 10, Lang::get(Lang::Msg::MenuSystemTitle),
                false, COL_WHITE, COL_LIGHTGRAY, true, true);

  m_osd->frame(24, 10, 16, 4, "Floppy",
                false, COL_YELLOW, COL_LIGHTGRAY, true, false);

  m_osd->text(26, 11, "3.5\" HD\n1440 KB",
              COL_WHITE, COL_LIGHTGRAY);

  m_osd->frame(40, 10, 16, 4, "Hard disk",
                false, COL_YELLOW, COL_LIGHTGRAY, true, false);

  m_osd->text(42, 11, "8 MB HDD", COL_WHITE, COL_LIGHTGRAY);

  int index = m_osd->menuBar(25, 15, 0, Lang::get(Lang::Msg::MenuDriveOptions), COL_LIGHTGRAY);
  if (index != -1) {

    m_osd->frame(4, 4, 72, 18, Lang::get(Lang::Msg::MenuDiskImage),
                  false, COL_WHITE, COL_LIGHTGRAY, true, true);

    sprintf(dir, "%s/%s", SD_MOUNT_PATH, cfg.disks_path);
    int res = m_osd->fileBrowser(5, 6, 70, 15, dir, filename);
    if (res != -1) {
      mount_disk_image(index, filename);
    }
  }

  shutdown();
  m_computer->video_resume();
  printf("osd: Mount floppy... out\n");
}

void Settings::mountHardDisk()
{
  static char dir[MAX_FILEPATH_LEN];
  static char filename[MAX_FILEPATH_LEN];

  printf("osd: Mount floppy... in\n");
  auto ctx = m_computer->video_suspend(CGA_MODE_TEXT_80x25_16COLORS);

  init(ctx->vram());

  m_osd->frame(0, 0, 80, 25, Lang::get(Lang::Msg::MenuTitle),
               true, COL_WHITE, COL_BLUE, false, false);

  m_osd->frame(22, 8, 35, 10, Lang::get(Lang::Msg::MenuSystemTitle),
                false, COL_WHITE, COL_LIGHTGRAY, true, true);

  m_osd->frame(24, 10, 16, 4, "Floppy",
                false, COL_YELLOW, COL_LIGHTGRAY, true, false);

  m_osd->text(26, 11, "3.5\" HD\n1440 KB",
              COL_WHITE, COL_LIGHTGRAY);

  m_osd->frame(40, 10, 16, 4, "Hard disk",
                false, COL_YELLOW, COL_LIGHTGRAY, true, false);

  m_osd->text(42, 11, "8 MB HDD", COL_WHITE, COL_LIGHTGRAY);

  int index = m_osd->menuBar(25, 15, 0, Lang::get(Lang::Msg::MenuDriveOptions), COL_LIGHTGRAY);
  if (index != -1) {

    m_osd->frame(4, 4, 72, 18, Lang::get(Lang::Msg::MenuDiskImage),
                  false, COL_WHITE, COL_LIGHTGRAY, true, true);

    sprintf(dir, "%s/%s", SD_MOUNT_PATH, cfg.disks_path);
    int res = m_osd->fileBrowser(5, 6, 70, 15, dir, filename);
    if (res != -1) {
      mount_disk_image(index, filename);
    }
  }

  shutdown();
  m_computer->video_resume();
  printf("osd: Mount floppy... out\n");
}

void Settings::init(uint8_t *vram)
{
  m_osd = new OSD(vram);
}

void Settings::shutdown()
{
  delete m_osd;
  m_osd = nullptr;
}

void Settings::update_progress_callback(void *ctx,
                                        int percent,
                                        const char *msg)
{
  auto settings = (Settings *) ctx;

  settings->m_osd->progress(27, 11, 26, percent, msg, true);
}

void Settings::mount_disk_image(const int index, char *filename)
{
  int ret;
  bool floppy = (index == 0 || index == 1);
  static char filepath[MAX_FILEPATH_LEN];

  m_osd->frame(25, 9, 30, 5, Lang::get(Lang::Msg::TitleUncompressing),
               false, COL_WHITE, COL_CYAN, true, true);

  m_osd->progress(27, 11, 26, 0, "", true);

  sprintf(filepath, "%s%s/tmpfs%d.img", SD_MOUNT_PATH, cfg.media_path, index);

  // Create a mount file image
  ret = vfs_fat_create_image(filepath, FAT_MOUNT_PATH, floppy);
  if (ret == VFS_FAT_OK) {

    // Unzip file disk
    sprintf(filepath, "%s%s/%s", SD_MOUNT_PATH, cfg.disks_path, filename);
    unzip_file_to_path(filepath, FAT_MOUNT_PATH, update_progress_callback, this);

    // Umount file image
    vfs_fat_unmount_image(FAT_MOUNT_PATH);
  }

  m_osd->frame(28, 8, 24, 8, Lang::get(Lang::Msg::TitleFinished),
               true, COL_WHITE, COL_CYAN, true, true);

  m_osd->text(30, 10, Lang::get(Lang::Msg::MsgUncompressed),
              COL_WHITE, COL_CYAN);

  ret = m_osd->menuBar(31, 13, 0, "Ok;Cancel", COL_CYAN);
  if (ret == 0) {
    sprintf(filepath, "tmpfs%d.img", index);

    if (floppy) {
      // After mounting a floppy image, the OSD closes and returns to the emulation
      m_computer->setDriveImage(index, filepath);
    } else {
      // Mounting a hard disk image will automatically trigger a system reboot to apply the changes
      m_computer->setDriveImage(index, filepath, 0, 0, 0);
      m_computer->reboot();
    }
  }
}
