#ifndef NINLIL_SX1262_RADIO_H
#define NINLIL_SX1262_RADIO_H

#include "ninlil.h"
#include "ninlil_rf_profile.h"
#include "ninlil_sx1262_hal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct ninlil_sx1262_rx_info {
    int8_t rssi_dbm;
    int8_t snr_db;
} ninlil_sx1262_rx_info;

typedef struct ninlil_sx1262_radio {
    ninlil_sx1262_hal_context hal;
    ninlil_rf_profile profile;
    TaskHandle_t owner_task;
    uint32_t crc_errors;
    uint32_t header_errors;
    uint32_t timeouts;
    uint32_t io_errors;
    bool configured;
    bool rx_active;
    bool isr_installed;
    bool rx_gate_active_high;
} ninlil_sx1262_radio;

int ninlil_sx1262_radio_init(ninlil_sx1262_radio *radio,
                             const ninlil_rf_profile *profile,
                             bool rx_gate_active_high);
void ninlil_sx1262_radio_deinit(ninlil_sx1262_radio *radio);
int ninlil_sx1262_radio_send(ninlil_sx1262_radio *radio, const uint8_t *data,
                             uint16_t length);
int ninlil_sx1262_radio_receive(ninlil_sx1262_radio *radio, uint8_t *data,
                                uint16_t capacity, uint16_t *length,
                                ninlil_sx1262_rx_info *info,
                                TickType_t wait_ticks);
int ninlil_sx1262_radio_recover(ninlil_sx1262_radio *radio);

#endif
