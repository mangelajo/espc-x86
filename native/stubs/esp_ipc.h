// ESP-IDF IPC stub
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef void (*esp_ipc_func_t)(void *arg);

inline esp_err_t esp_ipc_call_blocking(int cpu_id, esp_ipc_func_t func, void *arg) {
    func(arg);
    return ESP_OK;
}
