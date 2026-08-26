#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *awakened);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
#endif
