// FreeRTOS queue stub for native builds
#pragma once

#include <stdint.h>

#ifndef QueueHandle_t
typedef void * QueueHandle_t;
#endif

QueueHandle_t xQueueCreate(uint32_t length, uint32_t itemSize);
int xQueueSend(QueueHandle_t queue, const void *item, uint32_t timeout);
int xQueueSendToBack(QueueHandle_t queue, const void *item, uint32_t timeout);
int xQueueSendToFront(QueueHandle_t queue, const void *item, uint32_t timeout);
int xQueueReceive(QueueHandle_t queue, void *item, uint32_t timeout);
int xQueuePeek(QueueHandle_t queue, void *item, uint32_t timeout);
int uxQueueMessagesWaiting(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);

#define xQueueSendFromISR(q, i, w) xQueueSend(q, i, 0)
#define xQueueSendToBackFromISR(q, i, w) xQueueSendToBack(q, i, 0)
#define xQueueReceiveFromISR(q, i, w) xQueueReceive(q, i, 0)
