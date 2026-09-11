#ifndef NINLIL_PROBE_FIXTURE_RADIO_H
#define NINLIL_PROBE_FIXTURE_RADIO_H
#include "ninlil.h"
typedef struct ninlil_sx1262_radio {
    struct {
        int8_t tx_power_dbm;
    } profile;
    int8_t applied_power_dbm, requested_power_dbm;
} ninlil_sx1262_radio;
typedef struct ninlil_sx1262_rx_info {
    int fixture;
} ninlil_sx1262_rx_info;
int ninlil_sx1262_radio_airtime(ninlil_sx1262_radio *, uint16_t, uint32_t *);
int ninlil_sx1262_radio_power(ninlil_sx1262_radio *, int8_t);
int ninlil_sx1262_radio_receive(ninlil_sx1262_radio *, uint8_t *, uint16_t,
                                uint16_t *, ninlil_sx1262_rx_info *,
                                unsigned int);
int ninlil_sx1262_radio_send(ninlil_sx1262_radio *, const uint8_t *, uint16_t);
#endif
