// ESP32 I2S struct stub
#pragma once

#include <stdint.h>

typedef volatile struct {
    uint32_t reserved[64];
} i2s_dev_t;

extern i2s_dev_t I2S0;
extern i2s_dev_t I2S1;
