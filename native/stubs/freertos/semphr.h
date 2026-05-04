// FreeRTOS semaphore stub for native builds
#pragma once

#include <stdint.h>

#ifndef SemaphoreHandle_t
typedef void * SemaphoreHandle_t;
#endif

SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
int xSemaphoreTake(SemaphoreHandle_t sem, uint32_t timeout);
int xSemaphoreGive(SemaphoreHandle_t sem);
void vSemaphoreDelete(SemaphoreHandle_t sem);

#define xSemaphoreCreateRecursiveMutex() xSemaphoreCreateMutex()
#define xSemaphoreTakeRecursive(s, t) xSemaphoreTake(s, t)
#define xSemaphoreGiveRecursive(s) xSemaphoreGive(s)
