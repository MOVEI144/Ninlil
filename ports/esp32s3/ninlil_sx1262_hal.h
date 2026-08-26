#ifndef NINLIL_SX1262_HAL_H
#define NINLIL_SX1262_HAL_H

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct ninlil_sx1262_hal_context {
    spi_device_handle_t spi;
    gpio_num_t nss;
    gpio_num_t reset;
    gpio_num_t busy;
    uint32_t busy_timeout_ms;
    bool bus_initialized;
} ninlil_sx1262_hal_context;

int ninlil_sx1262_hal_init(ninlil_sx1262_hal_context *context);
void ninlil_sx1262_hal_deinit(ninlil_sx1262_hal_context *context);

#endif
