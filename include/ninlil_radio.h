#ifndef NINLIL_RADIO_H
#define NINLIL_RADIO_H

#include "ninlil.h"

#include <stddef.h>
#include <stdint.h>

#define NINLIL_RADIO_MTU 92u
#define NINLIL_RADIO_RX_SLOTS 4u
#define NINLIL_RADIO_RECOVERY_LIMIT 3u
#define NINLIL_RADIO_RECOVERY_WINDOW_MS UINT64_C(60000)

typedef enum ninlil_radio_state {
    NINLIL_RADIO_OFF = 0,
    NINLIL_RADIO_RESETTING,
    NINLIL_RADIO_STANDBY,
    NINLIL_RADIO_RX,
    NINLIL_RADIO_TX_PREPARE,
    NINLIL_RADIO_TX,
    NINLIL_RADIO_RECOVERING,
    NINLIL_RADIO_FAULT
} ninlil_radio_state;

typedef struct ninlil_radio_counters {
    uint32_t init_success;
    uint32_t init_failure;
    uint32_t reset_count;
    uint32_t busy_timeout_count;
    uint32_t spi_error_count;
    uint32_t rx_done_count;
    uint32_t tx_done_count;
    uint32_t rx_crc_error_count;
    uint32_t rx_header_error_count;
    uint32_t rx_ring_overflow_count;
    uint32_t tx_capacity_count;
    uint32_t tx_timeout_count;
    uint32_t tx_deferred_count;
    uint32_t link_packets_accepted;
    uint32_t link_packets_delivered;
} ninlil_radio_counters;

typedef struct ninlil_radio_link {
    ninlil_radio_state state;
    uint8_t tx_packet[NINLIL_RADIO_MTU];
    size_t tx_length;
    uint8_t tx_pending;
    uint8_t tx_in_flight;
    uint8_t rx_packets[NINLIL_RADIO_RX_SLOTS][NINLIL_RADIO_MTU];
    uint8_t rx_lengths[NINLIL_RADIO_RX_SLOTS];
    uint8_t rx_head;
    uint8_t rx_count;
    uint8_t recovery_attempts;
    uint8_t recovery_window_active;
    uint64_t recovery_window_start_ms;
    ninlil_radio_counters counters;
} ninlil_radio_link;

void ninlil_radio_link_init(ninlil_radio_link *radio);
void ninlil_radio_link_bind(ninlil_radio_link *radio, ninlil_link *link);
int ninlil_radio_begin_reset(ninlil_radio_link *radio);
int ninlil_radio_mark_initialized(ninlil_radio_link *radio);
int ninlil_radio_begin_tx(ninlil_radio_link *radio,
                          const uint8_t **packet,
                          size_t *length);
int ninlil_radio_tx_done(ninlil_radio_link *radio);
int ninlil_radio_tx_defer(ninlil_radio_link *radio);
int ninlil_radio_tx_timeout(ninlil_radio_link *radio, uint64_t now_ms);
int ninlil_radio_busy_timeout(ninlil_radio_link *radio, uint64_t now_ms);
int ninlil_radio_io_failure(ninlil_radio_link *radio, uint64_t now_ms);
int ninlil_radio_recovery_result(ninlil_radio_link *radio,
                                 uint64_t now_ms,
                                 int success);
int ninlil_radio_push_rx(ninlil_radio_link *radio,
                         const uint8_t *packet,
                         size_t length);
void ninlil_radio_note_crc_error(ninlil_radio_link *radio);
void ninlil_radio_note_header_error(ninlil_radio_link *radio);

#endif
