// FreeRTOS timers stub for native builds
#pragma once

#include <stdint.h>

#ifndef TimerHandle_t
typedef void * TimerHandle_t;
#endif

typedef void (*TimerCallbackFunction_t)(TimerHandle_t timer);

TimerHandle_t xTimerCreate(const char *name, uint32_t period, int autoReload,
                           void *timerId, TimerCallbackFunction_t callback);
int xTimerStart(TimerHandle_t timer, uint32_t timeout);
int xTimerStop(TimerHandle_t timer, uint32_t timeout);
int xTimerDelete(TimerHandle_t timer, uint32_t timeout);
int xTimerReset(TimerHandle_t timer, uint32_t timeout);
void *pvTimerGetTimerID(TimerHandle_t timer);
