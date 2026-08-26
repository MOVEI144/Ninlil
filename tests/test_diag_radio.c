#include "ninlil_diag.h"
#include "ninlil_radio.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int test_diag_codec(void)
{
    ninlil_diag_frame source;
    ninlil_diag_frame decoded;
    uint8_t packet[NINLIL_DIAG_MAX];
    size_t length;

    memset(&source, 0, sizeof(source));
    source.type = NINLIL_DIAG_PING;
    source.sequence = UINT32_C(0x12345678);
    source.source = UINT16_C(0x0102);
    source.target = UINT16_C(0x0304);
    source.payload_length = 4u;
    memcpy(source.payload, "ping", 4u);
    length = ninlil_diag_encode(packet, &source);
    CHECK(length == NINLIL_DIAG_HEADER + 4u);
    CHECK(ninlil_diag_decode(packet, length, &decoded) == NINLIL_OK);
    CHECK(decoded.type == source.type);
    CHECK(decoded.sequence == source.sequence);
    CHECK(decoded.source == source.source);
    CHECK(decoded.target == source.target);
    CHECK(decoded.payload_length == source.payload_length);
    CHECK(memcmp(decoded.payload, source.payload, source.payload_length) == 0);

    packet[12] = NINLIL_DIAG_MAX_PAYLOAD + 1u;
    CHECK(ninlil_diag_decode(packet, length, &decoded) == NINLIL_ERR_INVALID);
    packet[12] = 4u;
    packet[2] = 2u;
    CHECK(ninlil_diag_decode(packet, length, &decoded) == NINLIL_ERR_INVALID);
    CHECK(ninlil_diag_decode(packet, NINLIL_DIAG_HEADER - 1u, &decoded) ==
          NINLIL_ERR_INVALID);
    return 0;
}

static int initialize_radio(ninlil_radio_link *radio, ninlil_link *link)
{
    ninlil_radio_link_init(radio);
    ninlil_radio_link_bind(radio, link);
    CHECK(link->max_packet_size == NINLIL_RADIO_MTU);
    CHECK(ninlil_radio_begin_reset(radio) == NINLIL_OK);
    CHECK(ninlil_radio_mark_initialized(radio) == NINLIL_OK);
    CHECK(radio->state == NINLIL_RADIO_RX);
    return 0;
}

static int test_radio_tx_queue_and_state(void)
{
    ninlil_radio_link radio;
    ninlil_link link;
    uint8_t packet[16];
    const uint8_t *tx_packet;
    size_t tx_length;

    memset(packet, UINT8_C(0x5A), sizeof(packet));
    CHECK(initialize_radio(&radio, &link) == 0);
    CHECK(link.send(link.ctx, packet, sizeof(packet)) == NINLIL_OK);
    CHECK(link.send(link.ctx, packet, sizeof(packet)) == NINLIL_ERR_CAPACITY);
    CHECK(radio.counters.tx_capacity_count == 1u);
    CHECK(ninlil_radio_begin_tx(&radio, &tx_packet, &tx_length) == NINLIL_OK);
    CHECK(tx_length == sizeof(packet));
    CHECK(memcmp(tx_packet, packet, sizeof(packet)) == 0);
    CHECK(radio.state == NINLIL_RADIO_TX);
    CHECK(ninlil_radio_tx_defer(&radio) == NINLIL_OK);
    CHECK(radio.state == NINLIL_RADIO_RX);
    CHECK(radio.tx_pending == 1u);
    CHECK(radio.counters.tx_deferred_count == 1u);
    CHECK(ninlil_radio_begin_tx(&radio, &tx_packet, &tx_length) == NINLIL_OK);
    CHECK(ninlil_radio_tx_done(&radio) == NINLIL_OK);
    CHECK(radio.state == NINLIL_RADIO_RX);
    CHECK(radio.counters.tx_done_count == 1u);
    CHECK(link.send(link.ctx, packet, NINLIL_RADIO_MTU + 1u) ==
          NINLIL_ERR_TOO_LARGE);
    return 0;
}

static int test_radio_rx_ring(void)
{
    ninlil_radio_link radio;
    ninlil_link link;
    uint8_t packet[5] = {1u, 2u, 3u, 4u, 5u};
    uint8_t output[8];
    size_t length = 0u;
    unsigned int index;

    CHECK(initialize_radio(&radio, &link) == 0);
    for (index = 0u; index < NINLIL_RADIO_RX_SLOTS; index++)
        CHECK(ninlil_radio_push_rx(&radio, packet, sizeof(packet)) ==
              NINLIL_OK);
    CHECK(ninlil_radio_push_rx(&radio, packet, sizeof(packet)) ==
          NINLIL_ERR_CAPACITY);
    CHECK(radio.counters.rx_ring_overflow_count == 1u);
    for (index = 0u; index < NINLIL_RADIO_RX_SLOTS; index++) {
        CHECK(link.recv(link.ctx, output, sizeof(output), &length) == 1);
        CHECK(length == sizeof(packet));
        CHECK(memcmp(output, packet, sizeof(packet)) == 0);
    }
    CHECK(link.recv(link.ctx, output, sizeof(output), &length) == 0);
    return 0;
}

static int test_radio_bounded_recovery(void)
{
    ninlil_radio_link radio;
    ninlil_link link;
    uint8_t packet[4] = {9u, 8u, 7u, 6u};
    const uint8_t *tx_packet;
    size_t tx_length;

    CHECK(initialize_radio(&radio, &link) == 0);
    CHECK(link.send(link.ctx, packet, sizeof(packet)) == NINLIL_OK);
    CHECK(ninlil_radio_begin_tx(&radio, &tx_packet, &tx_length) == NINLIL_OK);
    CHECK(ninlil_radio_tx_timeout(&radio, 100u) == NINLIL_ERR_TIMEOUT);
    CHECK(radio.state == NINLIL_RADIO_RECOVERING);
    CHECK(radio.tx_pending == 1u);
    CHECK(ninlil_radio_recovery_result(&radio, 101u, 0) == NINLIL_ERR_IO);
    CHECK(ninlil_radio_recovery_result(&radio, 102u, 0) == NINLIL_ERR_IO);
    CHECK(ninlil_radio_recovery_result(&radio, 103u, 0) == NINLIL_ERR_FAULT);
    CHECK(radio.state == NINLIL_RADIO_FAULT);
    CHECK(link.send(link.ctx, packet, sizeof(packet)) == NINLIL_ERR_FAULT);
    CHECK(ninlil_radio_io_failure(&radio, NINLIL_RADIO_RECOVERY_WINDOW_MS +
                                              200u) == NINLIL_ERR_FAULT);
    CHECK(radio.state == NINLIL_RADIO_FAULT);
    CHECK(ninlil_radio_recovery_result(&radio,
                                       NINLIL_RADIO_RECOVERY_WINDOW_MS + 201u,
                                       1) == NINLIL_ERR_INVALID);
    return 0;
}

static int test_recovery_campaign_does_not_roll_over(void)
{
    ninlil_radio_link radio;
    ninlil_link link;
    uint64_t now_ms = UINT64_C(1);

    CHECK(initialize_radio(&radio, &link) == 0);
    CHECK(ninlil_radio_busy_timeout(&radio, now_ms) == NINLIL_ERR_TIMEOUT);
    CHECK(radio.recovery_attempts == 1u);

    now_ms += NINLIL_RADIO_RECOVERY_WINDOW_MS + UINT64_C(1);
    CHECK(ninlil_radio_recovery_result(&radio, now_ms, 0) == NINLIL_ERR_IO);
    CHECK(radio.recovery_attempts == 2u);

    now_ms += NINLIL_RADIO_RECOVERY_WINDOW_MS + UINT64_C(1);
    CHECK(ninlil_radio_recovery_result(&radio, now_ms, 0) == NINLIL_ERR_IO);
    CHECK(radio.recovery_attempts == 3u);

    now_ms += NINLIL_RADIO_RECOVERY_WINDOW_MS + UINT64_C(1);
    CHECK(ninlil_radio_recovery_result(&radio, now_ms, 0) == NINLIL_ERR_FAULT);
    CHECK(radio.state == NINLIL_RADIO_FAULT);
    return 0;
}

static int (*const tests[])(void) = {
    test_diag_codec,
    test_radio_tx_queue_and_state,
    test_radio_rx_ring,
    test_radio_bounded_recovery,
    test_recovery_campaign_does_not_roll_over,
};

int main(void)
{
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();
        printf("radio_%02zu %s\n", index + 1u, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
