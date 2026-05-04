// ESP32 FRC (Free Running Counter) timer register stub
// Used by i8253_pit for high-resolution timing
#pragma once

#include <stdint.h>

#define FRC_TIMER_PRESCALER_1    0
#define FRC_TIMER_PRESCALER_16   (1 << 1)
#define FRC_TIMER_PRESCALER_256  (1 << 2)
#define FRC_TIMER_ENABLE         (1 << 7)

// Register addresses (fake, used as array indices)
#define FRC_TIMER_LOAD_REG(i)    (0 + (i) * 3)
#define FRC_TIMER_COUNT_REG(i)   (1 + (i) * 3)
#define FRC_TIMER_CTRL_REG(i)    (2 + (i) * 3)

// Implemented in native_stubs.cpp - uses host monotonic clock
void     REG_WRITE(int reg, uint32_t value);
uint32_t REG_READ(int reg);
