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
    int64_t tx_not_before_us;
    int64_t rx_deadline_us;
    uint32_t channel_busy;
    uint8_t cca_stage;
    uint8_t cca_chip_mode;
    uint8_t cca_cmd_status;
    int16_t cca_rssi_dbm;
    int8_t requested_power_dbm;
    int8_t applied_power_dbm;
    bool configured;
    bool rx_active;
    bool isr_installed;
    bool rx_gate_active_high;
} ninlil_sx1262_radio;

int ninlil_sx1262_radio_init(ninlil_sx1262_radio *radio,
                             const ninlil_rf_profile *profile,
                             bool rx_gate_active_high);
void ninlil_sx1262_radio_deinit(ninlil_sx1262_radio *radio);
int ninlil_sx1262_radio_airtime(const ninlil_sx1262_radio *radio,
                                uint16_t length, uint32_t *airtime_us);
int ninlil_sx1262_radio_send(ninlil_sx1262_radio *radio, const uint8_t *data,
                             uint16_t length);
int ninlil_sx1262_radio_receive(ninlil_sx1262_radio *radio, uint8_t *data,
                                uint16_t capacity, uint16_t *length,
                                ninlil_sx1262_rx_info *info,
                                TickType_t wait_ticks);
int ninlil_sx1262_radio_recover(ninlil_sx1262_radio *radio);
/* Exclusive owner call. Wake with recover; no TX or RX while asleep. */
int ninlil_sx1262_radio_sleep(ninlil_sx1262_radio *radio);
/* Stages power for the next TX standby boundary; never exceeds profile max. */
int ninlil_sx1262_radio_power(ninlil_sx1262_radio *radio, int8_t power_dbm);

#endif
