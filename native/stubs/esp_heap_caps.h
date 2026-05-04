// ESP-IDF heap capabilities stub - maps to standard malloc/free
#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#define MALLOC_CAP_SPIRAM    (1 << 0)
#define MALLOC_CAP_INTERNAL  (1 << 1)
#define MALLOC_CAP_8BIT      (1 << 2)
#define MALLOC_CAP_DMA       (1 << 3)
#define MALLOC_CAP_32BIT     (1 << 4)

inline void *heap_caps_malloc(size_t size, uint32_t /*caps*/) { return malloc(size); }
inline void *heap_caps_calloc(size_t n, size_t size, uint32_t /*caps*/) { return calloc(n, size); }
inline void *heap_caps_realloc(void *ptr, size_t size, uint32_t /*caps*/) { return realloc(ptr, size); }
inline void  heap_caps_free(void *ptr) { free(ptr); }

inline size_t heap_caps_get_total_size(uint32_t caps) {
    if (caps == MALLOC_CAP_SPIRAM) return 8 * 1024 * 1024;   // 8 MB fake PSRAM
    if (caps == MALLOC_CAP_INTERNAL) return 520 * 1024;       // 520 KB fake DRAM
    return 0;
}

inline size_t heap_caps_get_free_size(uint32_t caps) {
    return heap_caps_get_total_size(caps) / 2;  // pretend half is free
}

inline size_t heap_caps_get_largest_free_block(uint32_t /*caps*/) { return 1024 * 1024; }
