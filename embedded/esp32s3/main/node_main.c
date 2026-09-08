#include <stdbool.h>

#include "bootloader_random.h"
#include "driver/usb_serial_jtag.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "ninlil_network_pump.h"
#include "node_example.h"
#include <stdio.h>
#include <string.h>

void app_main(void);
static ninlil_node *node;
static ninlil_sx1262_radio radio;
static ninlil_esp_network_pump pump;
static uint64_t expires;
static uint8_t faults;
static int fault;
static char line[259];

static uint64_t milliseconds(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}
static uint64_t get(const uint8_t *bytes, size_t size)
{
    uint64_t value = 0u;
    for (size_t i = 0u; i < size; i++)
        value = (value << 8) | bytes[i];
    return value;
}
static void put(uint8_t *bytes, uint64_t value, size_t size)
{
    while (size) {
        bytes[--size] = (uint8_t)value;
        value >>= 8;
    }
}
static int respond(char command, int result, const uint8_t *data, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    char output[259];
    int prefix =
        snprintf(output, sizeof(output), "NODE %c %d ", command, result);
    size_t at;
    if (prefix < 0 || (size_t)prefix + size * 2u + 1u > sizeof(output))
        return NINLIL_ERR_TOO_LARGE;
    at = (size_t)prefix;
    for (size_t i = 0u; i < size; i++) {
        output[at++] = digits[data[i] >> 4];
        output[at++] = digits[data[i] & 15u];
    }
    output[at++] = '\n';
    return usb_serial_jtag_write_bytes(output, at, pdMS_TO_TICKS(100)) ==
                   (int)at
               ? NINLIL_OK
               : NINLIL_ERR_IO;
}
static int accept_rx(void *ctx, const uint8_t *frame, size_t length)
{
    (void)ctx;
    if (length <= NINLIL_SECURE_OVERHEAD || memcmp(frame, "NS\001", 3u) != 0)
        return 1;
    if ((faults & 1u) &&
        ((get(frame + 4, 2u) == 1u && get(frame + 6, 2u) == 3u) ||
         (get(frame + 4, 2u) == 3u && get(frame + 6, 2u) == 1u)))
        return 0;
    return !((faults & 2u) && CONFIG_NINLIL_NODE_ID == 3 && frame[31] == 0u);
}
static ninlil_rf_profile profile(void)
{
    ninlil_rf_profile p = {0};
#ifdef CONFIG_NINLIL_RF_TX_ENABLE
    p.tx_enabled = true;
#endif
#ifdef CONFIG_NINLIL_RF_GATE_POLARITY_CONFIRMED
    p.rf_gate_polarity_confirmed = true;
#endif
    p.region = CONFIG_NINLIL_RF_REGION;
    p.frequency_hz = CONFIG_NINLIL_RF_FREQUENCY_HZ;
    p.tx_power_dbm = CONFIG_NINLIL_RF_TX_POWER_DBM;
    p.spreading_factor = CONFIG_NINLIL_RF_SF;
#if defined(CONFIG_NINLIL_RF_BW_500)
    p.bandwidth_hz = 500000u;
#elif defined(CONFIG_NINLIL_RF_BW_250)
    p.bandwidth_hz = 250000u;
#else
    p.bandwidth_hz = 125000u;
#endif
    p.coding_rate_denominator = CONFIG_NINLIL_RF_CR_DENOMINATOR;
    p.preamble_symbols = CONFIG_NINLIL_RF_PREAMBLE_SYMBOLS;
    return p;
}
static void stop(void)
{
    ninlil_node_close(node);
    node = NULL;
    node_application_close();
    if (radio.configured)
        ninlil_sx1262_radio_deinit(&radio);
    expires = 0u;
}
static int start(uint64_t duration)
{
    ninlil_rf_profile p = profile();
    int rc;
    bool high = false;
#ifdef CONFIG_NINLIL_RF_GATE_RX_ACTIVE_HIGH
    high = true;
#endif
    if (node || !duration || duration > 600000u || !p.tx_enabled ||
        !p.frequency_hz || !node_identity.signing_key ||
        !node_config.member_count)
        return NINLIL_ERR_STATE;
    rc = ninlil_node_open(&node, &node_config, milliseconds());
    if (rc == NINLIL_OK)
        rc = node_application_open("node_app", 0);
    if (rc == NINLIL_OK)
        rc = ninlil_sx1262_radio_init(&radio, &p, high);
    if (rc == NINLIL_OK)
        rc = ninlil_esp_node_open(&pump, &radio, node, 800000u);
    if (rc != NINLIL_OK) {
        stop();
        return rc;
    }
    pump.accept_rx = accept_rx;
    expires = milliseconds() + duration;
    fault = 0;
    return NINLIL_OK;
}
static int execute(char command, const uint8_t *data, size_t size,
                   uint8_t *output, size_t *written)
{
    *written = 0u;
    if (command == 'I' && !size) {
        if (!node_identity.signing_key)
            return NINLIL_ERR_EMPTY;
        memcpy(output, node_identity.identity, 32u);
        memcpy(output + 32, node_identity.public_key, 65u);
        *written = 97u;
        return NINLIL_OK;
    }
    if (command == 'P' && !size && !node)
        return node_storage_provision();
    if (command == 'G' && size == 4u)
        return start(get(data, 4u));
    if (command == 'X' && !size) {
        stop();
        return NINLIL_OK;
    }
    if (command == 'H' && !size) {
        ninlil_node_status status = {0};
        if (node)
            (void)ninlil_node_inspect(node, &status);
        memset(output, 0, 40u);
        output[0] = node ? 1u : 0u;
        output[1] = status.joined;
        output[2] = status.lease_clock_ready;
        output[3] = faults;
        put(output + 4, status.authenticated_peers, 2u);
        put(output + 6, status.active_members, 2u);
        put(output + 8, status.relay_owned, 2u);
        put(output + 10, node_application_count(), 2u);
        put(output + 12, pump.transmitted, 4u);
        put(output + 16, pump.received, 4u);
        put(output + 20, pump.discarded, 4u);
        put(output + 24, esp_get_free_heap_size(), 4u);
        put(output + 28, esp_get_minimum_free_heap_size(), 4u);
        put(output + 32, uxTaskGetStackHighWaterMark(NULL), 4u);
        put(output + 36, (uint32_t)(fault ? fault : status.fault), 4u);
        put(output + 40, status.handshakes, 4u);
        put(output + 44, status.rejected_frames, 4u);
        put(output + 48, status.handshake_peer, 2u);
        output[50] = status.handshake_stage;
        output[51] = status.handshake_initiator;
        put(output + 52, radio.channel_busy, 4u);
        put(output + 56, radio.crc_errors, 4u);
        put(output + 60, radio.header_errors, 4u);
        put(output + 64, radio.timeouts, 4u);
        put(output + 68, (uint32_t)pump.last_receive_result, 4u);
        put(output + 72, status.pending_route_epoch, 8u);
        put(output + 80, status.prepared_route_epoch, 8u);
        output[88] = status.pending_phase;
        output[89] = status.prepared_mask;
        output[90] = status.proof_mask;
        output[91] = status.authority_routes;
        output[92] = status.local_routes;
        output[93] = status.effective_routes;
        put(output + 94, status.wanted_routes, 2u);
        *written = 96u;
        return NINLIL_OK;
    }
    if (command == 'F' && size == 1u && data[0] <= 3u) {
#ifdef CONFIG_NINLIL_NODE_HIL_ENABLE
        faults = data[0];
        return NINLIL_OK;
#else
        return NINLIL_ERR_UNAUTHORIZED;
#endif
    }
    if (!node)
        return NINLIL_ERR_STATE;
    if (command == 'B' && size == 2u) {
        ninlil_node_peer_status status;
        int rc =
            ninlil_node_peer_inspect(node, (uint16_t)get(data, 2u), &status);
        if (rc != NINLIL_OK)
            return rc;
        output[0] = status.active;
        output[1] = status.revoked;
        output[2] = status.ready;
        output[3] = status.authority_state;
        output[4] = status.authority_phase;
        put(output + 5, status.probe_attempts, 2u);
        put(output + 7, status.probe_delivered, 2u);
        memcpy(output + 9, status.e2e_fingerprint, 16u);
        memcpy(output + 25, status.hop_fingerprint, 16u);
        *written = 41u;
        return NINLIL_OK;
    }
    if (command == 'L' && size == 4u) {
        ninlil_node_flow_status status;
        int rc = ninlil_node_flow_inspect(node, (uint16_t)get(data, 2u),
                                          (uint16_t)get(data + 2, 2u), &status);
        if (rc != NINLIL_OK)
            return rc;
        put(output, status.lease_ms, 8u);
        put(output + 8, status.local.epoch, 8u);
        put(output + 16, status.local.valid_until_ms, 8u);
        put(output + 24, status.authority.epoch, 8u);
        put(output + 32, status.authority.valid_until_ms, 8u);
        output[40] = status.local_ready;
        output[41] = (uint8_t)status.authority.phase;
        output[42] = status.reconciled;
        output[43] = status.notified;
        output[44] = status.local.path.count;
        output[55] = status.authority.path.count;
        for (unsigned int i = 0u; i < 5u; i++) {
            put(output + 45u + i * 2u, status.local.path.nodes[i], 2u);
            put(output + 56u + i * 2u, status.authority.path.nodes[i], 2u);
        }
        *written = 66u;
        return NINLIL_OK;
    }
    if (command == 'S' && size == 6u) {
        ninlil_id id;
        int rc = node_application_submit(ninlil_node_core(node),
                                         (uint16_t)get(data, 2u),
                                         (uint32_t)get(data + 2, 4u), &id);
        if (rc == NINLIL_OK) {
            memcpy(output, id.bytes, 16u);
            *written = 16u;
        }
        return rc;
    }
    if (command == 'Q' && size == 16u) {
        ninlil_id id;
        ninlil_info info;
        int rc;
        memcpy(id.bytes, data, 16u);
        rc = ninlil_query(ninlil_node_core(node), &id, &info);
        if (rc == NINLIL_OK) {
            output[0] = (uint8_t)info.outcome;
            output[1] = (uint8_t)info.latest_evidence;
            *written = 2u;
        }
        return rc;
    }
    if (command == 'D' && !size) {
        output[0] = ninlil_node_ready_remove(node) ? 1u : 0u;
        *written = 1u;
        return NINLIL_OK;
    }
    if (command == 'D' && size == 1u && data[0] <= 1u)
        return ninlil_node_relay_drain(node, data[0]);
    if (command == 'V' && size == 10u)
        return ninlil_node_revoke(node, (uint16_t)get(data, 2u),
                                  get(data + 2, 8u));
    return NINLIL_ERR_INVALID;
}
static int digit(char c)
{
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
}
static void console(void)
{
    static size_t used;
    static uint8_t overflow;
    char input[64];
    int got = usb_serial_jtag_read_bytes(input, sizeof(input), 0u);
    for (int i = 0; i < got; i++) {
        uint8_t data[128], output[128];
        size_t size = 0u, written = 0u;
        int rc = NINLIL_ERR_INVALID;
        if (input[i] != '\n') {
            if (used < sizeof(line))
                line[used++] = input[i];
            else
                overflow = 1u;
            continue;
        }
        if (!overflow && used >= 2u && line[1] == ' ' && !(used % 2u)) {
            size = (used - 2u) / 2u;
            rc = NINLIL_OK;
            for (size_t j = 0u; j < size; j++) {
                int a = digit(line[2u + j * 2u]), b = digit(line[3u + j * 2u]);
                if (a < 0 || b < 0) {
                    rc = NINLIL_ERR_INVALID;
                    break;
                }
                data[j] = (uint8_t)(a * 16 + b);
            }
        }
        if (rc == NINLIL_OK && line[0] == 'R' && !size) {
            (void)respond('R', NINLIL_OK, NULL, 0u);
            (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
            esp_restart();
        }
        if (rc == NINLIL_OK)
            rc = execute(line[0], data, size, output, &written);
        (void)respond(used ? line[0] : '?', rc, output,
                      rc == NINLIL_OK ? written : 0u);
        used = 0u;
        overflow = 0u;
    }
}
void app_main(void)
{
    usb_serial_jtag_driver_config_t config = {4096u, 4096u};
    int rc;
    if (usb_serial_jtag_driver_install(&config) != ESP_OK)
        return;
    /* This LoRa-only example owns the SAR ADC entropy source for its lifetime.
     * It does not start Wi-Fi, Bluetooth, ADC or I2S application drivers. */
    bootloader_random_enable();
    rc = node_storage_open();
    node_config.emit_ctx = &pump;
    (void)respond('!', rc, NULL, 0u);
    for (;;) {
        console();
        if (node && milliseconds() >= expires)
            stop();
        if (node) {
            rc = ninlil_esp_network_step(&pump);
            {
                ninlil_node_status status;
                (void)ninlil_node_inspect(node, &status);
                if (status.fault) {
                    fault = status.fault;
                    stop();
                    continue;
                }
            }
            if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_TIMEOUT) {
                rc = ninlil_sx1262_radio_recover(&radio);
                if (rc != NINLIL_OK) {
                    fault = rc;
                    stop();
                }
            } else if (rc == NINLIL_ERR_CORRUPT || rc == NINLIL_ERR_FAULT) {
                fault = rc;
                stop();
            }
            if (node) {
                rc = node_application_step(ninlil_node_core(node));
                if (rc != NINLIL_OK) {
                    fault = rc;
                    stop();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
