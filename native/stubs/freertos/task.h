// FreeRTOS task stub for native builds
#pragma once

#include <stdint.h>

#ifndef TaskHandle_t
typedef void * TaskHandle_t;
#endif

typedef void (*TaskFunction_t)(void *);

inline int xPortGetCoreID() { return 0; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }

// Implemented in native_stubs.cpp
void vTaskDelay(uint32_t ticks);
void vTaskDelete(TaskHandle_t handle);
int xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stackSize,
                            void *param, int priority, TaskHandle_t *handle, int core);

inline int xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stackSize,
                       void *param, int priority, TaskHandle_t *handle) {
    return xTaskCreatePinnedToCore(fn, name, stackSize, param, priority, handle, 0);
}
