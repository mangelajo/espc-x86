// ESP-IDF NVS stub
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef uint32_t nvs_handle_t;
typedef nvs_handle_t nvs_handle;

typedef enum {
    NVS_READONLY  = 0,
    NVS_READWRITE = 1,
} nvs_open_mode_t;

typedef nvs_open_mode_t nvs_open_mode;

inline esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle) {
    *handle = 1;
    return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) {}
inline esp_err_t nvs_commit(nvs_handle_t handle) { return ESP_OK; }

inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *len) {
    return ESP_ERR_NOT_FOUND;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *val, size_t len) {
    return ESP_OK;
}
inline esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out) {
    return ESP_ERR_NOT_FOUND;
}
inline esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t val) { return ESP_OK; }
