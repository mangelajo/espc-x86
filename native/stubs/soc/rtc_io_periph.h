// ESP32 RTC I/O peripheral stub
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t reg;
    uint32_t mux;
    uint32_t func;
    uint32_t ie;
    uint32_t pullup;
    uint32_t pulldown;
    uint32_t slpsel;
    uint32_t slpie;
    uint32_t hold;
    uint32_t hold_force;
    uint32_t drv_v;
    uint32_t drv_s;
    int      rtc_num;
} rtc_io_desc_t;

extern const rtc_io_desc_t rtc_io_desc[];
extern const int rtc_io_num_map[];
