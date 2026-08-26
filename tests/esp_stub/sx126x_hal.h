#ifndef SX126X_HAL_H
#define SX126X_HAL_H
#include <stdint.h>
typedef enum sx126x_hal_status_e {
    SX126X_HAL_STATUS_OK = 0,
    SX126X_HAL_STATUS_ERROR = 1
} sx126x_hal_status_t;
sx126x_hal_status_t sx126x_hal_reset(const void *context);
sx126x_hal_status_t sx126x_hal_wakeup(const void *context);
sx126x_hal_status_t sx126x_hal_write(const void *context,
                                     const uint8_t *command,
                                     uint16_t command_length,
                                     const uint8_t *data, uint16_t data_length);
sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *command,
                                    uint16_t command_length, uint8_t *data,
                                    uint16_t data_length);
#endif
