/*
 * Based on FabGL - ESP32 Graphics Library
 * Original project by Fabrizio Di Vittorio
 * https://github.com/fdivitto/FabGL
 *
 * Original Copyright (c) 2019-2022 Fabrizio Di Vittorio
 *
 * Modifications and further development:
 * Copyright (c) 2026 Jesus Martinez-Mateo
 * Author: Jesus Martinez-Mateo <jesus.martinez.mateo@gmail.com>
 *
 * This file is part of a derived work from FabGL
 * and is distributed under the terms of the
 * GNU General Public License version 3 or later.
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

#include "esp_intr_alloc.h"

#ifdef NATIVE_BUILD
#include <time.h>
#endif

namespace fabgl {

/**
 * @brief This class helps to choice a core for intensive processing tasks
 */
struct CoreUsage {

  static int busiestCore()                 { return s_busiestCore; }
  static int quietCore()                   { return s_busiestCore != -1 ? s_busiestCore ^ 1 : -1; }
  static void setBusiestCore(int core)     { s_busiestCore = core; }

  private:
    static int s_busiestCore;  // 0 = core 0, 1 = core 1 (default is FABGLIB_VIDEO_CPUINTENSIVE_TASKS_CORE)
};

void esp_intr_alloc_pinnedToCore(int source, int flags, intr_handler_t handler, void * arg, intr_handle_t * ret_handle, int core);

#ifdef NATIVE_BUILD
inline uint32_t getCycleCount() {
  // Use host clock as a rough cycle counter substitute
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000000000ULL + ts.tv_nsec);
}
#else
inline __attribute__((always_inline)) uint32_t getCycleCount() {
  uint32_t ccount;
  __asm__ __volatile__(
    "esync \n\t"
    "rsr %0, ccount \n\t"
    : "=a" (ccount)
  );
  return ccount;
}
#endif

} // end of namespace
