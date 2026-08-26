#include "ninlil_radio.h"

#include <string.h>

static int enter_recovery(ninlil_radio_link *radio, uint64_t now_ms,
                          int reported_error)
{
    int recovery_in_progress;
    int window_expired;

    if (radio->state == NINLIL_RADIO_FAULT)
        return NINLIL_ERR_FAULT;

    recovery_in_progress = radio->state == NINLIL_RADIO_RECOVERING ||
                           radio->state == NINLIL_RADIO_RESETTING;
    window_expired = radio->recovery_window_active &&
                     now_ms >= radio->recovery_window_start_ms &&
                     now_ms - radio->recovery_window_start_ms >=
                         NINLIL_RADIO_RECOVERY_WINDOW_MS;

    /*
     * A single recovery campaign never receives a fresh attempt budget merely
     * because hardware operations ran for longer than the nominal window.
     * Only a later, independent failure after a stable RX period may start a
     * new window. A backwards monotonic timestamp also fails closed.
     */
    if (!radio->recovery_window_active ||
        (!recovery_in_progress && window_expired)) {
        radio->recovery_window_active = 1u;
        radio->recovery_window_start_ms = now_ms;
        radio->recovery_attempts = 0u;
    }
    if (radio->recovery_attempts >= NINLIL_RADIO_RECOVERY_LIMIT) {
        radio->state = NINLIL_RADIO_FAULT;
        return NINLIL_ERR_FAULT;
    }
    radio->recovery_attempts++;
    radio->state = NINLIL_RADIO_RECOVERING;
    return reported_error;
}

static int link_send(void *ctx, const uint8_t *data, size_t length)
{
    ninlil_radio_link *radio = ctx;

    if (!radio || !data || length == 0u)
        return NINLIL_ERR_INVALID;
    if (length > NINLIL_RADIO_MTU)
        return NINLIL_ERR_TOO_LARGE;
    if (radio->state == NINLIL_RADIO_FAULT)
        return NINLIL_ERR_FAULT;
    if (radio->state != NINLIL_RADIO_RX && radio->state != NINLIL_RADIO_STANDBY)
        return NINLIL_ERR_BUSY;
    if (radio->tx_pending || radio->tx_in_flight) {
        radio->counters.tx_capacity_count++;
        return NINLIL_ERR_CAPACITY;
    }
    memcpy(radio->tx_packet, data, length);
    radio->tx_length = length;
    radio->tx_pending = 1u;
    radio->counters.link_packets_accepted++;
    return NINLIL_OK;
}

static int link_recv(void *ctx, uint8_t *buffer, size_t capacity,
                     size_t *length)
{
    ninlil_radio_link *radio = ctx;
    uint8_t index;
    size_t packet_length;

    if (!radio || !buffer || !length)
        return NINLIL_ERR_INVALID;
    if (radio->rx_count == 0u)
        return 0;
    index = radio->rx_head;
    packet_length = radio->rx_lengths[index];
    if (packet_length > capacity)
        return NINLIL_ERR_TOO_LARGE;
    memcpy(buffer, radio->rx_packets[index], packet_length);
    *length = packet_length;
    radio->rx_head = (uint8_t)((index + 1u) % NINLIL_RADIO_RX_SLOTS);
    radio->rx_count--;
    radio->counters.link_packets_delivered++;
    return 1;
}

void ninlil_radio_link_init(ninlil_radio_link *radio)
{
    if (!radio)
        return;
    memset(radio, 0, sizeof(*radio));
    radio->state = NINLIL_RADIO_OFF;
}

void ninlil_radio_link_bind(ninlil_radio_link *radio, ninlil_link *link)
{
    if (!radio || !link)
        return;
    link->send = link_send;
    link->recv = link_recv;
    link->ctx = radio;
    link->max_packet_size = NINLIL_RADIO_MTU;
}

int ninlil_radio_begin_reset(ninlil_radio_link *radio)
{
    if (!radio || radio->state == NINLIL_RADIO_FAULT ||
        (radio->state != NINLIL_RADIO_OFF &&
         radio->state != NINLIL_RADIO_STANDBY &&
         radio->state != NINLIL_RADIO_RECOVERING))
        return NINLIL_ERR_INVALID;
    if (radio->state == NINLIL_RADIO_OFF) {
        radio->recovery_attempts = 0u;
        radio->recovery_window_active = 0u;
    }
    radio->state = NINLIL_RADIO_RESETTING;
    radio->counters.reset_count++;
    return NINLIL_OK;
}

int ninlil_radio_mark_initialized(ninlil_radio_link *radio)
{
    if (!radio || radio->state != NINLIL_RADIO_RESETTING)
        return NINLIL_ERR_INVALID;
    radio->state = NINLIL_RADIO_RX;
    radio->counters.init_success++;
    return NINLIL_OK;
}

int ninlil_radio_begin_tx(ninlil_radio_link *radio, const uint8_t **packet,
                          size_t *length)
{
    if (!radio || !packet || !length || !radio->tx_pending ||
        radio->state != NINLIL_RADIO_RX)
        return NINLIL_ERR_INVALID;
    radio->state = NINLIL_RADIO_TX_PREPARE;
    radio->tx_pending = 0u;
    radio->tx_in_flight = 1u;
    radio->state = NINLIL_RADIO_TX;
    *packet = radio->tx_packet;
    *length = radio->tx_length;
    return NINLIL_OK;
}

int ninlil_radio_tx_done(ninlil_radio_link *radio)
{
    if (!radio || radio->state != NINLIL_RADIO_TX || !radio->tx_in_flight)
        return NINLIL_ERR_INVALID;
    radio->tx_in_flight = 0u;
    radio->tx_length = 0u;
    radio->state = NINLIL_RADIO_RX;
    radio->counters.tx_done_count++;
    return NINLIL_OK;
}

int ninlil_radio_tx_defer(ninlil_radio_link *radio)
{
    if (!radio || radio->state != NINLIL_RADIO_TX || !radio->tx_in_flight)
        return NINLIL_ERR_INVALID;
    radio->tx_in_flight = 0u;
    radio->tx_pending = 1u;
    radio->state = NINLIL_RADIO_RX;
    radio->counters.tx_deferred_count++;
    return NINLIL_OK;
}

int ninlil_radio_tx_timeout(ninlil_radio_link *radio, uint64_t now_ms)
{
    if (!radio || radio->state != NINLIL_RADIO_TX || !radio->tx_in_flight)
        return NINLIL_ERR_INVALID;
    radio->tx_in_flight = 0u;
    radio->tx_pending = 1u;
    radio->counters.tx_timeout_count++;
    return enter_recovery(radio, now_ms, NINLIL_ERR_TIMEOUT);
}

int ninlil_radio_busy_timeout(ninlil_radio_link *radio, uint64_t now_ms)
{
    if (!radio)
        return NINLIL_ERR_INVALID;
    if (radio->tx_in_flight) {
        radio->tx_in_flight = 0u;
        radio->tx_pending = 1u;
    }
    radio->counters.busy_timeout_count++;
    return enter_recovery(radio, now_ms, NINLIL_ERR_TIMEOUT);
}

int ninlil_radio_io_failure(ninlil_radio_link *radio, uint64_t now_ms)
{
    if (!radio)
        return NINLIL_ERR_INVALID;
    if (radio->tx_in_flight) {
        radio->tx_in_flight = 0u;
        radio->tx_pending = 1u;
    }
    radio->counters.spi_error_count++;
    return enter_recovery(radio, now_ms, NINLIL_ERR_IO);
}

int ninlil_radio_recovery_result(ninlil_radio_link *radio, uint64_t now_ms,
                                 int success)
{
    if (!radio || (radio->state != NINLIL_RADIO_RECOVERING &&
                   radio->state != NINLIL_RADIO_RESETTING))
        return NINLIL_ERR_INVALID;
    if (success) {
        radio->state = NINLIL_RADIO_RX;
        radio->counters.init_success++;
        return NINLIL_OK;
    }
    radio->counters.init_failure++;
    return enter_recovery(radio, now_ms, NINLIL_ERR_IO);
}

int ninlil_radio_push_rx(ninlil_radio_link *radio, const uint8_t *packet,
                         size_t length)
{
    uint8_t index;

    if (!radio || !packet || length == 0u)
        return NINLIL_ERR_INVALID;
    if (length > NINLIL_RADIO_MTU)
        return NINLIL_ERR_TOO_LARGE;
    if (radio->state != NINLIL_RADIO_RX)
        return radio->state == NINLIL_RADIO_FAULT ? NINLIL_ERR_FAULT
                                                  : NINLIL_ERR_BUSY;
    if (radio->rx_count >= NINLIL_RADIO_RX_SLOTS) {
        radio->counters.rx_ring_overflow_count++;
        return NINLIL_ERR_CAPACITY;
    }
    index =
        (uint8_t)((radio->rx_head + radio->rx_count) % NINLIL_RADIO_RX_SLOTS);
    memcpy(radio->rx_packets[index], packet, length);
    radio->rx_lengths[index] = (uint8_t)length;
    radio->rx_count++;
    radio->counters.rx_done_count++;
    return NINLIL_OK;
}

void ninlil_radio_note_crc_error(ninlil_radio_link *radio)
{
    if (radio)
        radio->counters.rx_crc_error_count++;
}

void ninlil_radio_note_header_error(ninlil_radio_link *radio)
{
    if (radio)
        radio->counters.rx_header_error_count++;
}
