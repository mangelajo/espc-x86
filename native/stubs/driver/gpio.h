// ESP-IDF GPIO driver stub
#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;

#define GPIO_NUM_MAX  40

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT,
    GPIO_MODE_OUTPUT_OD,
    GPIO_MODE_INPUT_OUTPUT,
    GPIO_MODE_INPUT_OUTPUT_OD,
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLUP_ENABLE,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_PULLDOWN_ENABLE,
} gpio_pulldown_t;

typedef enum {
    GPIO_INTR_DISABLE = 0,
    GPIO_INTR_POSEDGE,
    GPIO_INTR_NEGEDGE,
    GPIO_INTR_ANYEDGE,
    GPIO_INTR_LOW_LEVEL,
    GPIO_INTR_HIGH_LEVEL,
} gpio_int_type_t;

typedef struct {
    uint64_t       pin_bit_mask;
    gpio_mode_t    mode;
    gpio_pullup_t  pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

inline esp_err_t gpio_config(const gpio_config_t *cfg) { return ESP_OK; }
inline esp_err_t gpio_set_direction(gpio_num_t gpio, gpio_mode_t mode) { return ESP_OK; }
inline esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level) { return ESP_OK; }
inline int gpio_get_level(gpio_num_t gpio) { return 0; }
inline esp_err_t gpio_set_pull_mode(gpio_num_t gpio, int mode) { return ESP_OK; }
inline esp_err_t gpio_pullup_en(gpio_num_t gpio) { return ESP_OK; }
inline esp_err_t gpio_pullup_dis(gpio_num_t gpio) { return ESP_OK; }
inline esp_err_t gpio_pulldown_en(gpio_num_t gpio) { return ESP_OK; }
inline esp_err_t gpio_pulldown_dis(gpio_num_t gpio) { return ESP_OK; }

// GPIO matrix
inline esp_err_t gpio_matrix_in(uint32_t gpio, uint32_t signal_idx, bool inv) { return ESP_OK; }
inline esp_err_t gpio_matrix_out(uint32_t gpio, uint32_t signal_idx, bool out_inv, bool oen_inv) { return ESP_OK; }
