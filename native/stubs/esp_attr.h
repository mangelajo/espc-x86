// ESP-IDF attributes stub
#pragma once

#ifdef NATIVE_BUILD
// Emulation task check - called from the CPU loop
void taskEmuCheck();
#endif

#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define EXT_RAM_ATTR
#define __NOINIT_ATTR
#define WORD_ALIGNED_ATTR  __attribute__((aligned(4)))

#define ESP_EARLY_LOGE(tag, fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
