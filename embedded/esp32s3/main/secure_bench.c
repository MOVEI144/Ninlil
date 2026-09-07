#include "secure_bench.h"
#include "driver/usb_serial_jtag.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "ninlil_control_fragment.h"
#include "ninlil_control_log.h"
#include "ninlil_security_partitions.h"
#include <psa/crypto.h>
#include <stdio.h>
#include <string.h>

/* One bounded command/reply at a time. USB supplies test orchestration only;
 * T/R always use the physical SX1262; E/G/O/S/U execute on this MCU. The bench
 * does not claim to exercise the production network pump or Relay topology. */
static ninlil_edhoc handshake;
static ninlil_secure_session session;
static ninlil_counter_store counter;
static ninlil_counter_config counter_config;
static ninlil_esp_security_partition partition;
static ninlil_security_io counter_io;
static ninlil_control_reassembly assembly;
static ninlil_join_record saved;
static ninlil_control_log *log_store;
static ninlil_join_authority authority;
static ninlil_join_peer peers[2];
static uint8_t local_identity[32], peer_identity[32];
static uint8_t input[1024], output[1024];
static char line[2051], reply[2100];
static const uint8_t authority_id[16] = "Ninlil USB lab";

static uint64_t milliseconds(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int decode(const char *text, size_t size, size_t *length)
{
    size_t i;
    if (size % 2u != 0u || size / 2u > sizeof(input))
        return NINLIL_ERR_INVALID;
    for (i = 0u; i < size; i += 2u) {
        int a = hex_digit(text[i]), b = hex_digit(text[i + 1u]);
        if (a < 0 || b < 0)
            return NINLIL_ERR_INVALID;
        input[i / 2u] = (uint8_t)(a * 16 + b);
    }
    *length = size / 2u;
    return NINLIL_OK;
}

static int respond(char command, int rc, size_t length)
{
    static const char digits[] = "0123456789abcdef";
    int prefix = snprintf(reply, sizeof(reply), "BENCH %c %d ", command, rc);
    size_t i, offset;
    if (prefix < 0 || (size_t)prefix + length * 2u + 1u > sizeof(reply))
        return NINLIL_ERR_CAPACITY;
    offset = (size_t)prefix;
    for (i = 0u; i < length; i++) {
        reply[offset++] = digits[output[i] >> 4];
        reply[offset++] = digits[output[i] & 15u];
    }
    reply[offset++] = '\n';
    return usb_serial_jtag_write_bytes(reply, offset, pdMS_TO_TICKS(1000)) ==
                   (int)offset
               ? NINLIL_OK
               : NINLIL_ERR_IO;
}

static int restore_join(void *ctx, const ninlil_join_record *record)
{
    (void)ctx;
    saved = *record;
    return NINLIL_OK;
}

static int reopen_log(void)
{
    ninlil_control_replay replay = {0};
    ninlil_control_log_close(log_store);
    log_store = NULL;
    memset(&saved, 0, sizeof(saved));
    replay.join = restore_join;
    {
        int rc = ninlil_control_log_open(&log_store, "ninlil_control", 0x20000u,
                                         replay);
        authority.commit_ctx = log_store;
        return rc;
    }
}

static int approve(void *ctx, const uint8_t identity[32],
                   ninlil_join_grant *grant)
{
    (void)ctx;
    if (memcmp(identity, peer_identity, 32u) != 0)
        return NINLIL_ERR_UNAUTHORIZED;
    memset(grant, 0, sizeof(*grant));
    memcpy(grant->identity, identity, 32u);
    memcpy(grant->authority, authority_id, 16u);
    grant->node = CONFIG_NINLIL_PEER_ID;
    grant->membership_epoch = 1u;
    grant->binding_epoch = 1u;
    grant->capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
    grant->role = NINLIL_ROLE_POWERED_ENDPOINT;
    grant->service_count = 1u;
    grant->services[0] =
        (ninlil_service_grant){256u, 200u, 16u, NINLIL_SERVICE_BOTH, 15u};
    return NINLIL_OK;
}

static int open_session(size_t *written)
{
    ninlil_session_material material;
    int rc = ninlil_edhoc_material(&handshake, &material, peer_identity);
    if (rc != NINLIL_OK)
        return rc;
    ninlil_secure_close(&session);
    ninlil_counter_close(&counter);
    rc = ninlil_esp_session_counter_io(&partition, &counter_io, 0u);
    memset(&counter_config, 0, sizeof(counter_config));
    memcpy(counter_config.session_fingerprint, material.fingerprint, 16u);
    counter_config.direction = CONFIG_NINLIL_NODE_ID == 1 ? 0u : 1u;
    counter_config.reservation_size = 32u;
    /* 32-counter reservations must also fit the store's 32-bit generation. */
    counter_config.max_counter_exclusive = UINT64_C(1000000);
    /* Fresh authenticated material only; this explicit bench operation reuses
     * slot 0. The operator backs up all Flash before enabling this image. */
    if (rc == NINLIL_OK)
        rc = ninlil_security_format(&counter_io);
    if (rc == NINLIL_OK)
        rc = ninlil_counter_open(&counter, &counter_io,
                                 NINLIL_COUNTER_CREATE_NEW, &counter_config);
    if (rc == NINLIL_OK)
        rc =
            ninlil_secure_open(&session, &material, &counter, ninlil_psa_aead(),
                               CONFIG_NINLIL_NODE_ID, CONFIG_NINLIL_PEER_ID,
                               counter_config.direction);
    if (rc == NINLIL_OK) {
        memcpy(output, material.fingerprint, 16u);
        *written = 16u;
    }
    ninlil_secret_clear(&material, sizeof(material));
    ninlil_edhoc_close(&handshake); /* O cannot reuse the same material. */
    return rc;
}

static int join_command(char command, size_t length, size_t *written)
{
    ninlil_join_record record, ack;
    int rc;
    if (!log_store || !session.ready)
        return NINLIL_ERR_STATE;
    if (command == 'A' && length == 0u && CONFIG_NINLIL_NODE_ID == 1) {
        rc =
            ninlil_join_open(&authority, peers, 2u, authority_id,
                             ninlil_control_log_join, log_store, approve, NULL);
        if (rc == NINLIL_OK)
            rc = ninlil_join_begin(&authority, peer_identity, milliseconds());
        if (rc == NINLIL_OK)
            rc = ninlil_join_authenticated(&authority, peer_identity,
                                           session.material.fingerprint,
                                           milliseconds());
        if (rc == NINLIL_OK)
            rc = ninlil_join_prepare(&authority, peer_identity, milliseconds(),
                                     &record);
        if (rc == NINLIL_OK)
            *written = ninlil_join_encode(&record, output, sizeof(output));
        return rc;
    }
    rc = ninlil_join_decode(input, length, &record);
    if (rc != NINLIL_OK)
        return rc;
    if (command == 'Q' && CONFIG_NINLIL_NODE_ID == 2) {
        rc = ninlil_join_endpoint_commit(
            &record, saved.state ? &saved : NULL, local_identity, authority_id,
            session.material.fingerprint, ninlil_control_log_join, log_store,
            &ack);
        if (rc == NINLIL_OK) {
            saved = ack;
            *written = ninlil_join_encode(&ack, output, sizeof(output));
        }
        return rc;
    }
    if (command == 'V' && CONFIG_NINLIL_NODE_ID == 1)
        return ninlil_join_confirm(
            &authority, &record, session.material.fingerprint, milliseconds());
    return NINLIL_ERR_INVALID;
}

static int radio_command(ninlil_sx1262_radio *radio, char command,
                         size_t length, size_t *written)
{
    uint16_t received = 0u;
    uint64_t deadline = milliseconds() + 2000u;
    int rc;
    if (command == 'R' && length == 0u) {
        rc = ninlil_sx1262_radio_receive(radio, output, 240u, &received, NULL,
                                         pdMS_TO_TICKS(1000));
        if (rc == NINLIL_OK)
            *written = received;
        return rc;
    }
    if (command != 'T' || length == 0u || length > 240u)
        return NINLIL_ERR_INVALID;
    do {
        rc = ninlil_sx1262_radio_send(radio, input, (uint16_t)length);
        if (rc != NINLIL_ERR_BUSY)
            return rc;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (milliseconds() < deadline);
    return NINLIL_ERR_TIMEOUT;
}

static int execute(ninlil_sx1262_radio *radio, char command, size_t length,
                   size_t *written)
{
    int rc;
    *written = 0u;
    if (command == 'I' && length == 0u) {
        rc = ninlil_bench_identity(output);
        if (rc == NINLIL_OK)
            *written = 65u;
        return rc;
    }
    if (command == 'P' && length == 65u) {
        ninlil_secure_close(&session);
        ninlil_edhoc_close(&handshake);
        return ninlil_bench_handshake(&handshake, input, milliseconds());
    }
    if (command == 'E')
        return ninlil_edhoc_exchange(&handshake, length ? input : NULL, length,
                                     milliseconds(), output, sizeof(output),
                                     written);
    if (command == 'O' && length == 0u)
        return open_session(written);
    if (command == 'S' || command == 'C')
        return command == 'S'
                   ? ninlil_secure_seal(&session, input, length, output,
                                        sizeof(output), written)
                   : ninlil_secure_seal_control(&session, input, length, output,
                                                sizeof(output), written);
    if (command == 'U' || command == 'D')
        return command == 'U'
                   ? ninlil_secure_unseal(&session, input, length, output,
                                          sizeof(output), written)
                   : ninlil_secure_unseal_control(&session, input, length,
                                                  output, sizeof(output),
                                                  written);
    if (command == 'T' || command == 'R')
        return radio_command(radio, command, length, written);
    if (command == 'G')
        return ninlil_control_reassemble(&assembly, input, length,
                                         milliseconds(), output, sizeof(output),
                                         written);
    if (command == 'F' && length == 0u) {
        ninlil_control_reassembly_clear(&assembly);
        return NINLIL_OK;
    }
    if (command == 'K' && length == 0u && session.ready) {
        ninlil_counter_close(&counter);
        return ninlil_counter_open(&counter, &counter_io,
                                   NINLIL_COUNTER_RESUME_EXISTING,
                                   &counter_config);
    }
    if (command == 'A' || command == 'Q' || command == 'V')
        return join_command(command, length, written);
    if (command == 'L' && length == 0u && CONFIG_NINLIL_NODE_ID == 1) {
        ninlil_peer_policy policy;
        return ninlil_join_policy(&authority, CONFIG_NINLIL_PEER_ID, &policy);
    }
    if (command == 'W' && length == 0u && CONFIG_NINLIL_NODE_ID == 1) {
        rc = ninlil_join_revoke(&authority, peer_identity);
        ninlil_secure_close(&session);
        return rc;
    }
    if (command == 'H' && length == 0u) {
        uint32_t values[3] = {uxTaskGetStackHighWaterMark(NULL),
                              esp_get_free_heap_size(),
                              esp_get_minimum_free_heap_size()};
        size_t i, j;
        for (i = 0u; i < 3u; i++)
            for (j = 0u; j < 4u; j++)
                output[i * 4u + j] = (uint8_t)(values[i] >> (24u - 8u * j));
        *written = 12u;
        return NINLIL_OK;
    }
    if (command == 'Z' && length == 0u) {
        rc = reopen_log();
        if (rc == NINLIL_OK && saved.state)
            *written = ninlil_join_encode(&saved, output, sizeof(output));
        return rc;
    }
    if (command == 'X' && length == 0u) {
        ninlil_secure_close(&session);
        ninlil_edhoc_close(&handshake);
        return NINLIL_OK;
    }
    return NINLIL_ERR_INVALID;
}

void ninlil_secure_bench(ninlil_sx1262_radio *radio)
{
    usb_serial_jtag_driver_config_t config = {4096u, 4096u};
    size_t used = 0u;
    bool overflow = false;
    uint64_t expires = milliseconds() + 1800000u;
    if (usb_serial_jtag_driver_install(&config) != ESP_OK)
        return;
    /* No identities or sessions are loaded from durable membership state. */
    if (ninlil_bench_identity(output) != NINLIL_OK || reopen_log() != NINLIL_OK)
        goto cleanup;
    {
        size_t hash_length;
        if (psa_hash_compute(PSA_ALG_SHA_256, output, 65u, local_identity, 32u,
                             &hash_length) != PSA_SUCCESS ||
            hash_length != 32u)
            goto cleanup;
    }
    (void)respond('!', NINLIL_OK, 0u);
    while (milliseconds() < expires) {
        char c;
        size_t length = 0u, written = 0u;
        int rc;
        if (usb_serial_jtag_read_bytes(&c, 1u, pdMS_TO_TICKS(100)) != 1)
            continue;
        if (c != '\n') {
            if (used < sizeof(line))
                line[used++] = c;
            else
                overflow = true;
            continue;
        }
        rc = overflow || used < 2u || line[1] != ' '
                 ? NINLIL_ERR_INVALID
                 : decode(line + 2u, used - 2u, &length);
        if (rc == NINLIL_OK)
            rc = execute(radio, line[0], length, &written);
        if (respond(used ? line[0] : '?', rc, rc == NINLIL_OK ? written : 0u) !=
            NINLIL_OK)
            break;
        used = 0u;
        overflow = false;
    }
cleanup:
    ninlil_secure_close(&session);
    ninlil_edhoc_close(&handshake);
    ninlil_control_log_close(log_store);
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
    (void)usb_serial_jtag_driver_uninstall();
}
