// FreeRTOS stub for native builds
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

// Include commonly-needed ESP-IDF stubs that the real FreeRTOS/ESP-IDF
// build system provides transitively
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"

// Basic FreeRTOS types
typedef void * TaskHandle_t;
typedef void * SemaphoreHandle_t;
typedef void * QueueHandle_t;
typedef void * TimerHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE   1
#define pdFALSE  0
#define pdPASS   pdTRUE
#define pdFAIL   pdFALSE

#define portMAX_DELAY  0xFFFFFFFFUL
#define portTICK_PERIOD_MS  1
#define configTICK_RATE_HZ  1000

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

// Task macros
#define tskIDLE_PRIORITY  0

// Include sub-headers
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
