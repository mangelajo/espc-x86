// ESP32 ROM lldesc stub
#pragma once

#include <stdint.h>

typedef struct lldesc_s {
    volatile uint32_t size    : 12;
    volatile uint32_t length  : 12;
    volatile uint32_t offset  : 5;
    volatile uint32_t sosf    : 1;
    volatile uint32_t eof     : 1;
    volatile uint32_t owner   : 1;
    volatile uint8_t  *buf;
    volatile struct lldesc_s *empty;
    volatile struct lldesc_s *qe_next;
} lldesc_t;
