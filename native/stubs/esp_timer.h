// ESP-IDF timer stub - uses host clock
#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef void * esp_timer_handle_t;

typedef enum {
    ESP_TIMER_TASK = 0,
    ESP_TIMER_ISR  = 1,
} esp_timer_dispatch_t;

typedef void (*esp_timer_cb_t)(void *arg);

typedef struct {
    esp_timer_cb_t       callback;
    void                *arg;
    esp_timer_dispatch_t dispatch_method;
    const char          *name;
    bool                 skip_unhandled_events;
} esp_timer_create_args_t;

// Implemented in native_stubs.cpp
#ifdef __cplusplus
extern "C" {
#endif
int64_t esp_timer_get_time(void);
#ifdef __cplusplus
}
#endif

esp_err_t esp_timer_init(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
bool      esp_timer_is_active(esp_timer_handle_t timer);
