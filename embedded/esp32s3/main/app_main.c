#include "ninlil.h"
#include "ninlil_diag.h"
#include "ninlil_radio.h"
#include "ninlil_rf_profile.h"
#include "ninlil_sx1262_radio.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define JOURNAL_LABEL "ninlil_journal"
#define APP_SERVICE UINT16_C(0x0100)
#define LOOP_DELAY_MS 10u
#define RECOVERY_DELAY_MS 20u
#define STACK_MIN_FREE_PERCENT 25u
#define DIAG_PING_PERIOD_MS 100u
#define DIAG_PING_DEADLINE_MS 1000u

static const char *const TAG = "ninlil_m1";

#if defined(CONFIG_NINLIL_RF_TX_ENABLE)
#define NINLIL_CONFIG_RF_TX_ENABLED true
#else
#define NINLIL_CONFIG_RF_TX_ENABLED false
#endif

#if defined(CONFIG_NINLIL_RF_GATE_POLARITY_CONFIRMED)
#define NINLIL_CONFIG_RF_GATE_CONFIRMED true
#else
#define NINLIL_CONFIG_RF_GATE_CONFIRMED false
#endif

#if defined(CONFIG_NINLIL_RF_GATE_RX_ACTIVE_HIGH)
#define NINLIL_CONFIG_RF_GATE_RX_ACTIVE_HIGH true
#else
#define NINLIL_CONFIG_RF_GATE_RX_ACTIVE_HIGH false
#endif

void app_main(void);

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

#if defined(CONFIG_NINLIL_M1_MODE_DIAGNOSTIC) &&                               \
    defined(CONFIG_NINLIL_DIAGNOSTIC_INITIATOR)
static bool tick_reached(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}
#endif

static int stack_headroom(const char *phase)
{
    UBaseType_t free_bytes = uxTaskGetStackHighWaterMark(NULL);
    uint32_t total_bytes = CONFIG_ESP_MAIN_TASK_STACK_SIZE;
    uint32_t percent =
        total_bytes == 0u ? 0u : ((uint32_t)free_bytes * 100u) / total_bytes;

    ESP_LOGI(TAG, "stack phase=%s minimum-free=%lu/%lu bytes (%lu%%)", phase,
             (unsigned long)free_bytes, (unsigned long)total_bytes,
             (unsigned long)percent);
    return percent >= STACK_MIN_FREE_PERCENT ? NINLIL_OK : NINLIL_ERR_FAULT;
}

static uint32_t configured_bandwidth(void)
{
#if defined(CONFIG_NINLIL_RF_BW_500)
    return 500000u;
#elif defined(CONFIG_NINLIL_RF_BW_250)
    return 250000u;
#else
    return 125000u;
#endif
}

static ninlil_rf_profile configured_profile(void)
{
    ninlil_rf_profile profile;

    memset(&profile, 0, sizeof(profile));
    profile.tx_enabled = NINLIL_CONFIG_RF_TX_ENABLED;
    profile.rf_gate_polarity_confirmed = NINLIL_CONFIG_RF_GATE_CONFIRMED;
    profile.region = CONFIG_NINLIL_RF_REGION;
    profile.frequency_hz = CONFIG_NINLIL_RF_FREQUENCY_HZ;
    profile.tx_power_dbm = CONFIG_NINLIL_RF_TX_POWER_DBM;
    profile.spreading_factor = CONFIG_NINLIL_RF_SF;
    profile.bandwidth_hz = configured_bandwidth();
    profile.coding_rate_denominator = CONFIG_NINLIL_RF_CR_DENOMINATOR;
    profile.preamble_symbols = CONFIG_NINLIL_RF_PREAMBLE_SYMBOLS;
    return profile;
}

static int initialize_radio_state(ninlil_radio_link *state)
{
    ninlil_radio_link_init(state);
    if (ninlil_radio_begin_reset(state) != NINLIL_OK)
        return NINLIL_ERR_FAULT;
    return ninlil_radio_mark_initialized(state);
}

static int recover_physical(ninlil_sx1262_radio *physical,
                            ninlil_radio_link *state, int failure)
{
    int rc;

    if (state->state == NINLIL_RADIO_FAULT)
        return NINLIL_ERR_FAULT;
    if (failure == NINLIL_ERR_TIMEOUT && state->state == NINLIL_RADIO_TX &&
        state->tx_in_flight)
        rc = ninlil_radio_tx_timeout(state, now_ms());
    else if (failure == NINLIL_ERR_TIMEOUT)
        rc = ninlil_radio_busy_timeout(state, now_ms());
    else
        rc = ninlil_radio_io_failure(state, now_ms());

    while (rc != NINLIL_ERR_FAULT) {
        int hardware_result = ninlil_sx1262_radio_recover(physical);

        rc = ninlil_radio_recovery_result(state, now_ms(),
                                          hardware_result == NINLIL_OK);
        if (rc == NINLIL_OK)
            return NINLIL_OK;
        if (rc != NINLIL_ERR_FAULT)
            vTaskDelay(pdMS_TO_TICKS(RECOVERY_DELAY_MS));
    }
    return NINLIL_ERR_FAULT;
}

#if defined(CONFIG_NINLIL_M1_MODE_DELIVERY)
static int random_fill(void *context, uint8_t *output, size_t length)
{
    (void)context;
    esp_fill_random(output, length);
    return 0;
}

static int delivery_policy_lookup(void *context, uint16_t peer,
                                  ninlil_peer_policy *policy)
{
    static const ninlil_service_grant grant = {
        .service_id = APP_SERVICE,
        .maximum_payload_bytes = NINLIL_MAX_PAYLOAD,
        .maximum_live_messages = 32u,
        .directions = NINLIL_SERVICE_BOTH,
        .traffic_class_mask = UINT8_C(0x0F),
    };

    (void)context;
    if (!policy || peer == 0u || peer == UINT16_MAX)
        return NINLIL_ERR_NOT_FOUND;
    memset(policy, 0, sizeof(*policy));
    policy->role = NINLIL_ROLE_POWERED_ENDPOINT;
    policy->capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
    policy->membership_epoch = 1u;
    policy->session_membership_epoch = 1u;
    policy->grants = &grant;
    policy->grant_count = 1u;
    return NINLIL_OK;
}

static int pump_radio_rx(ninlil_sx1262_radio *physical, ninlil_radio_link *link)
{
    unsigned int work;

    for (work = 0u; work < NINLIL_RADIO_RX_SLOTS; work++) {
        uint8_t packet[NINLIL_RADIO_MTU];
        ninlil_sx1262_rx_info info;
        uint16_t length = 0u;
        int rc = ninlil_sx1262_radio_receive(physical, packet, sizeof(packet),
                                             &length, &info, 0u);

        if (rc == NINLIL_ERR_EMPTY)
            return NINLIL_OK;
        if (rc == NINLIL_ERR_INVALID || rc == NINLIL_ERR_TIMEOUT)
            continue;
        if (rc != NINLIL_OK)
            return rc;
        ESP_LOGD(TAG, "RX len=%u RSSI=%d SNR=%d", (unsigned int)length,
                 (int)info.rssi_dbm, (int)info.snr_db);
        rc = ninlil_radio_push_rx(link, packet, length);
        if (rc != NINLIL_OK)
            return rc;
    }
    return NINLIL_OK;
}

static int pump_radio_tx(ninlil_sx1262_radio *physical, ninlil_radio_link *link)
{
    const uint8_t *packet;
    size_t length;
    int rc;

    if (!link->tx_pending)
        return NINLIL_OK;
    rc = ninlil_radio_begin_tx(link, &packet, &length);
    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_sx1262_radio_send(physical, packet, (uint16_t)length);
    if (rc == NINLIL_OK)
        return ninlil_radio_tx_done(link);
    if (rc == NINLIL_ERR_BUSY)
        return ninlil_radio_tx_defer(link);
    ESP_LOGW(TAG, "radio TX failed: %d; packet remains pending", rc);
    if (rc == NINLIL_ERR_TIMEOUT || rc == NINLIL_ERR_IO)
        return recover_physical(physical, link, rc);
    return rc;
}

static uint32_t get_be32(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | (uint32_t)input[3];
}

static int validate_hil_inbound(const ninlil_inbound *inbound)
{
    uint32_t sequence;

    if (!inbound || inbound->source != CONFIG_NINLIL_PEER_ID ||
        inbound->service != APP_SERVICE || inbound->payload_len != 8u ||
        inbound->payload[0] != (uint8_t)(CONFIG_NINLIL_PEER_ID >> 8) ||
        inbound->payload[1] != (uint8_t)CONFIG_NINLIL_PEER_ID ||
        inbound->payload[2] != (uint8_t)(CONFIG_NINLIL_NODE_ID >> 8) ||
        inbound->payload[3] != (uint8_t)CONFIG_NINLIL_NODE_ID)
        return NINLIL_ERR_INVALID;
    sequence = get_be32(inbound->payload + 4);
    return sequence >= 1u && sequence <= CONFIG_NINLIL_DELIVERY_MESSAGE_COUNT
               ? NINLIL_OK
               : NINLIL_ERR_INVALID;
}

#if defined(CONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT)
typedef struct hil_batch {
    uint32_t sequence;
    ninlil_id message_id;
    bool active;
    bool complete;
} hil_batch;

static void put_be32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void hil_key(ninlil_id *key, uint32_t sequence)
{
    static const uint8_t prefix[8] = {'N', 'I', 'N', 'L', 'I', 'L', 'M', '1'};

    memcpy(key->bytes, prefix, sizeof(prefix));
    key->bytes[8] = (uint8_t)(CONFIG_NINLIL_NODE_ID >> 8);
    key->bytes[9] = (uint8_t)CONFIG_NINLIL_NODE_ID;
    key->bytes[10] = (uint8_t)(CONFIG_NINLIL_PEER_ID >> 8);
    key->bytes[11] = (uint8_t)CONFIG_NINLIL_PEER_ID;
    put_be32(key->bytes + 12, sequence);
}

static void hil_payload(uint8_t payload[8], uint32_t sequence)
{
    payload[0] = (uint8_t)(CONFIG_NINLIL_NODE_ID >> 8);
    payload[1] = (uint8_t)CONFIG_NINLIL_NODE_ID;
    payload[2] = (uint8_t)(CONFIG_NINLIL_PEER_ID >> 8);
    payload[3] = (uint8_t)CONFIG_NINLIL_PEER_ID;
    put_be32(payload + 4, sequence);
}

static int advance_hil_batch(ninlil_runtime *runtime, hil_batch *batch)
{
    ninlil_info info;
    int rc;

    if (batch->complete)
        return NINLIL_OK;
    if (batch->active) {
        rc = ninlil_query(runtime, &batch->message_id, &info);
        if (rc != NINLIL_OK)
            return rc;
        if (info.outcome == NINLIL_OUTCOME_ACTIVE)
            return NINLIL_OK;
        if (info.outcome != NINLIL_OUTCOME_SATISFIED)
            return NINLIL_ERR_FAULT;
        ESP_LOGI(TAG, "HIL delivery satisfied sequence=%lu/%u",
                 (unsigned long)batch->sequence,
                 (unsigned int)CONFIG_NINLIL_DELIVERY_MESSAGE_COUNT);
        batch->active = false;
        batch->sequence++;
    }
    if (batch->sequence > CONFIG_NINLIL_DELIVERY_MESSAGE_COUNT) {
        batch->complete = true;
        ESP_LOGI(TAG,
                 "NINLIL_HIL_DELIVERY result=PASS node=%u peer=%u count=%u",
                 (unsigned int)CONFIG_NINLIL_NODE_ID,
                 (unsigned int)CONFIG_NINLIL_PEER_ID,
                 (unsigned int)CONFIG_NINLIL_DELIVERY_MESSAGE_COUNT);
        return stack_headroom("delivery-complete");
    }
    {
        ninlil_id key;
        uint8_t payload[8];

        hil_key(&key, batch->sequence);
        hil_payload(payload, batch->sequence);
        ninlil_submission request;

        memset(&request, 0, sizeof(request));
        request.struct_version = NINLIL_API_VERSION;
        request.idempotency_key = key;
        request.target = CONFIG_NINLIL_PEER_ID;
        request.service = APP_SERVICE;
        request.ownership = NINLIL_OWNERSHIP_DURABLE;
        request.required_evidence = NINLIL_EVIDENCE_REMOTE_STORED;
        request.traffic_class = NINLIL_TRAFFIC_NORMAL;
        request.payload = payload;
        request.payload_len = sizeof(payload);
        rc = ninlil_submit(runtime, &request, &batch->message_id);
        if (rc != NINLIL_OK)
            return rc;
        batch->active = true;
    }
    return NINLIL_OK;
}
#endif

static void run_delivery(ninlil_sx1262_radio *physical)
{
    ninlil_radio_link adapter;
    ninlil_link link;
    ninlil_config config;
    ninlil_runtime *runtime = NULL;
    uint32_t inbound_applied = 0u;
    bool fatal = false;
#if defined(CONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT)
    hil_batch batch = {.sequence = 1u};
#endif

    if (initialize_radio_state(&adapter) != NINLIL_OK) {
        ESP_LOGE(TAG, "Link state initialization failed");
        return;
    }
    memset(&link, 0, sizeof(link));
    ninlil_radio_link_bind(&adapter, &link);
    memset(&config, 0, sizeof(config));
    config.journal_location = JOURNAL_LABEL;
    config.node_id = CONFIG_NINLIL_NODE_ID;
    config.retry_interval_steps = 50u;
    config.max_work_per_step = 8u;
    config.link = link;
    config.random.fill = random_fill;
    config.policy_lookup = delivery_policy_lookup;
    if (ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                     &config.profile) != NINLIL_OK ||
        ninlil_open(&runtime, &config) != NINLIL_OK) {
        ESP_LOGE(TAG, "durable Runtime open failed");
        return;
    }
    while (!fatal) {
        ninlil_inbound inbound;
        int rc = pump_radio_rx(physical, &adapter);

        if (rc == NINLIL_ERR_IO)
            rc = recover_physical(physical, &adapter, rc);
        if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY) {
            ESP_LOGE(TAG, "Radio RX pump failed: %d", rc);
            fatal = true;
        }
        if (!fatal) {
            rc = ninlil_step(runtime);
            if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY &&
                rc != NINLIL_ERR_CONFLICT) {
                ESP_LOGE(TAG, "Runtime step failed: %d", rc);
                fatal = true;
            }
        }
        if (!fatal) {
            rc = pump_radio_tx(physical, &adapter);
            if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY) {
                ESP_LOGE(TAG, "Radio TX pump failed: %d", rc);
                fatal = true;
            }
        }
        while (!fatal && ninlil_receive(runtime, &inbound) == NINLIL_OK) {
            rc = validate_hil_inbound(&inbound);
            if (rc != NINLIL_OK) {
                ESP_LOGE(TAG, "HIL inbound validation failed: %d", rc);
                fatal = true;
                break;
            }
            rc = ninlil_application_accept(runtime, &inbound.message_id);
            if (rc != NINLIL_OK) {
                ESP_LOGE(TAG, "Application acceptance commit failed: %d", rc);
                fatal = true;
                break;
            }
            inbound_applied++;
            ESP_LOGI(TAG,
                     "Application ACCEPTED count=%lu service=%u len=%u",
                     (unsigned long)inbound_applied,
                     (unsigned int)inbound.service,
                     (unsigned int)inbound.payload_len);
        }
#if defined(CONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT)
        if (!fatal && NINLIL_CONFIG_RF_TX_ENABLED) {
            rc = advance_hil_batch(runtime, &batch);
            if (rc != NINLIL_OK) {
                ESP_LOGE(TAG, "HIL batch failed: %d", rc);
                fatal = true;
            }
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }
    ninlil_close(runtime);
}
#endif

#if defined(CONFIG_NINLIL_M1_MODE_DIAGNOSTIC)
static int diagnostic_send(ninlil_sx1262_radio *radio, uint8_t type,
                           uint32_t sequence, uint16_t target)
{
    ninlil_diag_frame frame;
    uint8_t packet[NINLIL_DIAG_MAX];
    size_t length;

    memset(&frame, 0, sizeof(frame));
    frame.type = type;
    frame.sequence = sequence;
    frame.source = CONFIG_NINLIL_NODE_ID;
    frame.target = target;
    length = ninlil_diag_encode(packet, &frame);
    if (length == 0u)
        return NINLIL_ERR_INVALID;
    return ninlil_sx1262_radio_send(radio, packet, (uint16_t)length);
}

static void run_diagnostic(ninlil_sx1262_radio *radio)
{
    ninlil_radio_link recovery_state;
#if defined(CONFIG_NINLIL_DIAGNOSTIC_INITIATOR)
    uint32_t sent = 0u;
    uint32_t received = 0u;
    uint32_t timed_out = 0u;
    uint32_t waiting_sequence = 0u;
    TickType_t next_send = xTaskGetTickCount();
    TickType_t deadline = 0u;
    bool waiting = false;
#endif

    if (initialize_radio_state(&recovery_state) != NINLIL_OK)
        return;
    for (;;) {
        uint8_t packet[NINLIL_DIAG_MAX];
        uint16_t length = 0u;
        ninlil_sx1262_rx_info info;
#if defined(CONFIG_NINLIL_DIAGNOSTIC_INITIATOR)
        TickType_t now = xTaskGetTickCount();
#endif
        int rc =
            ninlil_sx1262_radio_receive(radio, packet, sizeof(packet), &length,
                                        &info, pdMS_TO_TICKS(LOOP_DELAY_MS));

        if (rc == NINLIL_OK) {
            ninlil_diag_frame frame;

            if (ninlil_diag_decode(packet, length, &frame) == NINLIL_OK &&
                frame.target == CONFIG_NINLIL_NODE_ID) {
                ESP_LOGI(TAG, "DIAG type=%u seq=%lu RSSI=%d SNR=%d",
                         (unsigned int)frame.type,
                         (unsigned long)frame.sequence, (int)info.rssi_dbm,
                         (int)info.snr_db);
                if (frame.type == NINLIL_DIAG_PING &&
                    NINLIL_CONFIG_RF_TX_ENABLED) {
                    rc = diagnostic_send(radio, NINLIL_DIAG_PONG,
                                         frame.sequence, frame.source);
                    if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_TIMEOUT)
                        rc = recover_physical(radio, &recovery_state, rc);
                    if (rc != NINLIL_OK && rc != NINLIL_ERR_BUSY)
                        return;
                }
#if defined(CONFIG_NINLIL_DIAGNOSTIC_INITIATOR)
                if (frame.type == NINLIL_DIAG_PONG && waiting &&
                    frame.source == CONFIG_NINLIL_PEER_ID &&
                    frame.sequence == waiting_sequence) {
                    received++;
                    waiting = false;
                    next_send = now + pdMS_TO_TICKS(DIAG_PING_PERIOD_MS);
                }
#endif
            }
        } else if (rc == NINLIL_ERR_IO) {
            if (recover_physical(radio, &recovery_state, rc) != NINLIL_OK)
                return;
        }
#if defined(CONFIG_NINLIL_DIAGNOSTIC_INITIATOR)
        now = xTaskGetTickCount();
        if (waiting && tick_reached(now, deadline)) {
            timed_out++;
            waiting = false;
            next_send = now + pdMS_TO_TICKS(DIAG_PING_PERIOD_MS);
        }
        if (!waiting && sent < CONFIG_NINLIL_DIAGNOSTIC_PING_COUNT &&
            tick_reached(now, next_send)) {
            uint32_t sequence = sent + 1u;

            rc = diagnostic_send(radio, NINLIL_DIAG_PING, sequence,
                                 CONFIG_NINLIL_PEER_ID);
            if (rc == NINLIL_OK) {
                sent++;
                waiting_sequence = sequence;
                waiting = true;
                deadline = now + pdMS_TO_TICKS(DIAG_PING_DEADLINE_MS);
            } else if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_TIMEOUT) {
                if (recover_physical(radio, &recovery_state, rc) != NINLIL_OK)
                    return;
            } else if (rc != NINLIL_ERR_BUSY) {
                return;
            }
        }
        if (!waiting && sent == CONFIG_NINLIL_DIAGNOSTIC_PING_COUNT) {
            uint32_t minimum =
                ((uint32_t)CONFIG_NINLIL_DIAGNOSTIC_PING_COUNT * 995u + 999u) /
                1000u;
            bool passed = received >= minimum;

            ESP_LOGI(TAG,
                     "NINLIL_HIL_DIAG result=%s sent=%lu received=%lu "
                     "timeout=%lu minimum=%lu",
                     passed ? "PASS" : "FAIL", (unsigned long)sent,
                     (unsigned long)received, (unsigned long)timed_out,
                     (unsigned long)minimum);
            if (stack_headroom("diagnostic-complete") != NINLIL_OK)
                ESP_LOGE(TAG, "diagnostic stack headroom below 25%%");
            return;
        }
#endif
    }
}
#endif

static int initialize_radio_cycles(ninlil_sx1262_radio *radio,
                                   const ninlil_rf_profile *profile)
{
    unsigned int cycle;

    for (cycle = 1u; cycle <= CONFIG_NINLIL_RADIO_INIT_CYCLES; cycle++) {
        int rc = ninlil_sx1262_radio_init(radio, profile,
                                          NINLIL_CONFIG_RF_GATE_RX_ACTIVE_HIGH);

        if (rc != NINLIL_OK) {
            ESP_LOGE(TAG, "SX1262 initialization failed cycle=%u rc=%d", cycle,
                     rc);
            return rc;
        }
        if (cycle < CONFIG_NINLIL_RADIO_INIT_CYCLES)
            ninlil_sx1262_radio_deinit(radio);
    }
    ESP_LOGI(TAG, "NINLIL_HIL_INIT result=PASS cycles=%u",
             (unsigned int)CONFIG_NINLIL_RADIO_INIT_CYCLES);
    {
        int rc = stack_headroom("radio-init");

        if (rc != NINLIL_OK)
            ninlil_sx1262_radio_deinit(radio);
        return rc;
    }
}

void app_main(void)
{
    ninlil_sx1262_radio radio;
    ninlil_rf_profile profile = configured_profile();
    int rc;

    ESP_LOGI(TAG,
             "profile region='%s' freq=%lu tx=%s power=%d SF=%u BW=%lu "
             "CR=4/%u preamble=%u gate-confirmed=%s",
             profile.region, (unsigned long)profile.frequency_hz,
             profile.tx_enabled ? "enabled" : "disabled",
             (int)profile.tx_power_dbm, (unsigned int)profile.spreading_factor,
             (unsigned long)profile.bandwidth_hz,
             (unsigned int)profile.coding_rate_denominator,
             (unsigned int)profile.preamble_symbols,
             profile.rf_gate_polarity_confirmed ? "yes" : "no");
    rc = initialize_radio_cycles(&radio, &profile);
    if (rc != NINLIL_OK)
        return;
    if (profile.frequency_hz == 0u) {
        ESP_LOGW(TAG, "operational RF disabled: frequency is unset");
        ninlil_sx1262_radio_deinit(&radio);
        return;
    }
    if (!profile.tx_enabled) {
        ESP_LOGE(TAG, "M1 HIL operation requires explicit TX enable");
        ninlil_sx1262_radio_deinit(&radio);
        return;
    }
#if defined(CONFIG_NINLIL_M1_MODE_DIAGNOSTIC)
    run_diagnostic(&radio);
#else
    run_delivery(&radio);
#endif
    ninlil_sx1262_radio_deinit(&radio);
}
