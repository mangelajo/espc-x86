// ESP-IDF interrupt allocation stub
#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef void (*intr_handler_t)(void *arg);
typedef void * intr_handle_t;

#define ESP_INTR_FLAG_LEVEL1  (1 << 1)
#define ESP_INTR_FLAG_LEVEL2  (1 << 2)
#define ESP_INTR_FLAG_LEVEL3  (1 << 3)

inline esp_err_t esp_intr_alloc(int source, int flags, intr_handler_t handler,
                                void *arg, intr_handle_t *ret_handle) {
    return ESP_OK;
}

inline esp_err_t esp_intr_free(intr_handle_t handle) { return ESP_OK; }
