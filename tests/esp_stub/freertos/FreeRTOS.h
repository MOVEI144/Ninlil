#ifndef FREERTOS_H
#define FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef uint32_t UBaseType_t;
#define pdFALSE 0
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(value) ((TickType_t)(value))
#define portYIELD_FROM_ISR() ((void)0)
#endif
