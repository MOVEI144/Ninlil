#include "hil_delivery.h"
#include "ninlil.h"
#include "ninlil_diag.h"
#include "ninlil_radio.h"
#include "ninlil_rf_profile.h"
#include "ninlil_sx1262_radio.h"
#if defined(CONFIG_NINLIL_M1_MODE_SECURE_BENCH)
#include "secure_bench.h"
#endif

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(CONFIG_NINLIL_DELIVERY_FAULT_CAMPAIGN)
#include "driver/gpio.h"
#include "esp_system.h"
#endif

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
#define DIAG_START_DELAY_MS 3000u

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

#if !defined(CONFIG_NINLIL_M1_MODE_SECURE_BENCH)
static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

#endif

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

#if !defined(CONFIG_NINLIL_M1_MODE_SECURE_BENCH)
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

#endif

#if defined(CONFIG_NINLIL_M1_MODE_DELIVERY)
static const ninlil_hil_campaign delivery_campaign = {
    CONFIG_NINLIL_DELIVERY_CAMPAIGN_ID, CONFIG_NINLIL_NODE_ID,
    CONFIG_NINLIL_PEER_ID, CONFIG_NINLIL_DELIVERY_MESSAGE_COUNT};

static void format_id(const ninlil_id *id, char text[33])
{
    static const char hex[] = "0123456789abcdef";
    size_t index;

    for (index = 0u; index < NINLIL_ID_BYTES; index++) {
        text[index * 2u] = hex[id->bytes[index] >> 4];
        text[index * 2u + 1u] = hex[id->bytes[index] & 15u];
    }
    text[32] = '\0';
}

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
    if (!policy || peer != CONFIG_NINLIL_PEER_ID)
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

#if defined(CONFIG_NINLIL_DELIVERY_FAULT_CAMPAIGN)
static bool rx_fault_used[5];
static unsigned int receipt_fault_count;
static bool dio_fault_used;
static bool hold_receipt;
static ninlil_id held_id;

// Exact fixed-size HIL DATA only; never interpret an arbitrary radio frame.
static uint32_t fault_sequence(const uint8_t *packet, size_t length)
{
    uint32_t campaign;

    if (length != 52u || memcmp(packet, "NL\002\001", 4u) != 0)
        return 0u;
    campaign = ((uint32_t)packet[40] << 24) | ((uint32_t)packet[41] << 16) |
               ((uint32_t)packet[42] << 8) | (uint32_t)packet[43];
    if (campaign != delivery_campaign.campaign || packet[48] != 0u ||
        packet[49] != 0u || packet[50] != 0u)
        return 0u;
    return packet[51];
}

static void fault_log(const char *kind, const uint8_t *id_bytes,
                      uint32_t sequence)
{
    ninlil_id id;
    char text[33];

    memcpy(id.bytes, id_bytes, sizeof(id.bytes));
    format_id(&id, text);
    ESP_LOGI(TAG, "HIL_FAULT kind=%s seq=%lu id=%s", kind,
             (unsigned long)sequence, text);
}
#endif

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
#if defined(CONFIG_NINLIL_DELIVERY_FAULT_CAMPAIGN)
        {
            uint32_t sequence = fault_sequence(packet, length);
            bool duplicate = false;

            if (sequence >= 1u && sequence <= 4u && !rx_fault_used[sequence]) {
                rx_fault_used[sequence] = true;
                if (sequence == 1u) {
                    fault_log("drop-data-after-rx", packet + 10, sequence);
                    continue;
                }
                if (sequence == 2u) {
                    fault_log("duplicate-data-after-rx", packet + 10, sequence);
                    duplicate = true;
                } else if (sequence == 3u) {
                    fault_log("reserved-flags-after-rx", packet + 10, sequence);
                    packet[29] |= UINT8_C(0x80);
                } else {
                    fault_log("wrong-target-after-rx", packet + 10, sequence);
                    packet[6] = 0u;
                    packet[7] = 99u;
                }
            } else if (length == 26u && memcmp(packet, "NL\002\002", 4u) == 0 &&
                       receipt_fault_count < 2u) {
                receipt_fault_count++;
                if (receipt_fault_count == 1u) {
                    fault_log("drop-receipt-after-rx", packet + 8, 0u);
                    continue;
                }
                fault_log("duplicate-receipt-after-rx", packet + 8, 0u);
                duplicate = true;
            }
            // The injected restart clears this boot-local marker. A replayed
            // sequence 6 must be allowed to send its durable duplicate receipt.
            if (sequence == 6u && rx_fault_used[4]) {
                memcpy(held_id.bytes, packet + 10, sizeof(held_id.bytes));
                hold_receipt = true;
            }
            if (duplicate) {
                rc = ninlil_radio_push_rx(link, packet, length);
                if (rc != NINLIL_OK)
                    return rc;
            }
        }
#endif
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
#if defined(CONFIG_NINLIL_DELIVERY_FAULT_CAMPAIGN)
    if (hold_receipt && length == 26u &&
        memcmp(packet, "NL\002\002", 4u) == 0 &&
        memcmp(packet + 8, held_id.bytes, sizeof(held_id.bytes)) == 0) {
        fault_log("hold-receipt-until-restart", packet + 8, 6u);
        return ninlil_radio_tx_defer(link);
    }
    if (!dio_fault_used && fault_sequence(packet, length) == 5u) {
        if (gpio_intr_disable((gpio_num_t)39) != ESP_OK)
            return NINLIL_ERR_IO;
        rc = ninlil_sx1262_radio_send(physical, packet, (uint16_t)length);
        if (gpio_intr_enable((gpio_num_t)39) != ESP_OK)
            return NINLIL_ERR_IO;
        if (rc == NINLIL_ERR_BUSY)
            return ninlil_radio_tx_defer(link);
        dio_fault_used = true;
        fault_log("masked-dio1-tx", packet + 10, 5u);
        ESP_LOGI(TAG, "HIL_FAULT_TX_RESULT rc=%d expected=%d", rc,
                 NINLIL_ERR_TIMEOUT);
        if (rc != NINLIL_ERR_TIMEOUT)
            return NINLIL_ERR_FAULT;
    } else
#endif
        rc = ninlil_sx1262_radio_send(physical, packet, (uint16_t)length);
    if (rc == NINLIL_OK) {
        ESP_LOGI(TAG, "HIL_LINK_SENT bytes=%u", (unsigned int)length);
        return ninlil_radio_tx_done(link);
    }
    if (rc == NINLIL_ERR_BUSY)
        return ninlil_radio_tx_defer(link);
    ESP_LOGW(TAG, "radio TX failed: %d; packet remains pending", rc);
    if (rc == NINLIL_ERR_TIMEOUT || rc == NINLIL_ERR_IO)
        return recover_physical(physical, link, rc);
    return rc;
}

#if defined(CONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT)
typedef struct hil_batch {
    uint32_t sequence;
    ninlil_id message_id;
    bool active;
    bool complete;
} hil_batch;

static int advance_hil_batch(ninlil_runtime *runtime, hil_batch *batch)
{
    ninlil_info info;
    char id[33];
    int rc;

    if (batch->complete)
        return NINLIL_OK;
    if (batch->active) {
        rc = ninlil_query(runtime, &batch->message_id, &info);
        if (rc != NINLIL_OK)
            return rc;
        if (info.outcome == NINLIL_OUTCOME_ACTIVE)
            return NINLIL_OK;
        if (info.outcome != NINLIL_OUTCOME_SATISFIED ||
            info.required_evidence != NINLIL_EVIDENCE_REMOTE_STORED ||
            info.latest_evidence < NINLIL_EVIDENCE_REMOTE_STORED)
            return NINLIL_ERR_FAULT;
        format_id(&batch->message_id, id);
        ESP_LOGI(TAG, "HIL_SATISFIED campaign=%lu seq=%lu id=%s evidence=%u",
                 (unsigned long)delivery_campaign.campaign,
                 (unsigned long)batch->sequence, id,
                 (unsigned int)info.latest_evidence);
        batch->active = false;
        batch->sequence++;
    }
    if (batch->sequence > CONFIG_NINLIL_DELIVERY_MESSAGE_COUNT) {
        batch->complete = true;
        ESP_LOGI(TAG,
                 "NINLIL_HIL_DELIVERY result=PASS node=%u peer=%u count=%u "
                 "campaign=%lu",
                 (unsigned int)CONFIG_NINLIL_NODE_ID,
                 (unsigned int)CONFIG_NINLIL_PEER_ID,
                 (unsigned int)CONFIG_NINLIL_DELIVERY_MESSAGE_COUNT,
                 (unsigned long)delivery_campaign.campaign);
        return stack_headroom("delivery-complete");
    }
    {
        uint8_t payload[NINLIL_HIL_PAYLOAD_SIZE];
        ninlil_submission request;

        rc = ninlil_hil_request(&delivery_campaign, batch->sequence, &request,
                                payload);
        if (rc != NINLIL_OK)
            return rc;
        rc = ninlil_submit(runtime, &request, &batch->message_id);
        if (rc != NINLIL_OK)
            return rc;
        rc = ninlil_query(runtime, &batch->message_id, &info);
        if (rc != NINLIL_OK)
            return rc;
        format_id(&batch->message_id, id);
        ESP_LOGI(TAG,
                 "HIL_SUBMIT campaign=%lu seq=%lu id=%s outcome=%u evidence=%u",
                 (unsigned long)delivery_campaign.campaign,
                 (unsigned long)batch->sequence, id, (unsigned int)info.outcome,
                 (unsigned int)info.latest_evidence);
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
    uint32_t inbound_accepted = 0u;
    bool fatal = false;
#if defined(CONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT)
    hil_batch batch = {.sequence = 1u};
    uint64_t campaign_deadline = now_ms() + UINT64_C(600000);
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
    ESP_LOGI(TAG, "NINLIL_HIL_DELIVERY_READY campaign=%lu node=%u peer=%u",
             (unsigned long)delivery_campaign.campaign,
             (unsigned int)delivery_campaign.node,
             (unsigned int)delivery_campaign.peer);
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
            uint32_t sequence;
            char id[33];

            rc = ninlil_hil_inbound(&delivery_campaign, &inbound, &sequence);
            if (rc != NINLIL_OK) {
                ESP_LOGE(TAG, "HIL inbound validation failed: %d", rc);
                fatal = true;
                break;
            }
            format_id(&inbound.message_id, id);
            ESP_LOGI(
                TAG,
                "HIL_STORED campaign=%lu seq=%lu id=%s source=%u target=%u",
                (unsigned long)delivery_campaign.campaign,
                (unsigned long)sequence, id, (unsigned int)inbound.source,
                (unsigned int)delivery_campaign.node);
            // This test consumer only validates and durably retires the offer.
            // It does not represent any product application side effect.
            rc = ninlil_application_accept(runtime, &inbound.message_id);
            if (rc != NINLIL_OK) {
                ESP_LOGE(TAG, "Application acceptance commit failed: %d", rc);
                fatal = true;
                break;
            }
            inbound_accepted++;
            ESP_LOGI(TAG, "HIL_CONSUMED campaign=%lu seq=%lu id=%s count=%lu",
                     (unsigned long)delivery_campaign.campaign,
                     (unsigned long)sequence, id,
                     (unsigned long)inbound_accepted);
#if defined(CONFIG_NINLIL_DELIVERY_FAULT_CAMPAIGN)
            if (sequence == 6u && hold_receipt) {
                fault_log("receiver-restart-before-receipt",
                          inbound.message_id.bytes, sequence);
                esp_restart();
            }
#endif
        }
#if defined(CONFIG_NINLIL_DELIVERY_SUBMIT_ON_BOOT)
        if (!fatal && NINLIL_CONFIG_RF_TX_ENABLED) {
            if (!batch.complete && now_ms() >= campaign_deadline) {
                ESP_LOGE(TAG,
                         "NINLIL_HIL_DELIVERY result=FAIL reason=deadline");
                fatal = true;
                break;
            }
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
    int rc;

    memset(&frame, 0, sizeof(frame));
    frame.type = type;
    frame.sequence = sequence;
    frame.source = CONFIG_NINLIL_NODE_ID;
    frame.target = target;
    length = ninlil_diag_encode(packet, &frame);
    if (length == 0u)
        return NINLIL_ERR_INVALID;
    rc = ninlil_sx1262_radio_send(radio, packet, (uint16_t)length);
    if (rc != NINLIL_ERR_BUSY)
        ESP_LOGI(TAG,
                 "DIAG_TX type=%u seq=%lu source=%u target=%u rc=%d "
                 "cca_stage=%u chip=%u command=%u rssi=%d",
                 (unsigned int)type, (unsigned long)sequence,
                 (unsigned int)frame.source, (unsigned int)target, rc,
                 (unsigned int)radio->cca_stage,
                 (unsigned int)radio->cca_chip_mode,
                 (unsigned int)radio->cca_cmd_status, (int)radio->cca_rssi_dbm);
    return rc;
}

static void run_diagnostic(ninlil_sx1262_radio *radio)
{
    ninlil_radio_link recovery_state;
#if defined(CONFIG_NINLIL_DIAGNOSTIC_INITIATOR)
    uint32_t sent = 0u;
    uint32_t received = 0u;
    uint32_t timed_out = 0u;
    uint32_t waiting_sequence = 0u;
    TickType_t next_send =
        xTaskGetTickCount() + pdMS_TO_TICKS(DIAG_START_DELAY_MS);
    TickType_t deadline = 0u;
    bool waiting = false;
    uint64_t campaign_deadline =
        now_ms() + DIAG_START_DELAY_MS +
        (uint64_t)CONFIG_NINLIL_DIAGNOSTIC_PING_COUNT * UINT64_C(2000);
    uint64_t progress_at = now_ms() + UINT64_C(5000);
#endif

    if (initialize_radio_state(&recovery_state) != NINLIL_OK)
        return;
    ESP_LOGI(TAG, "NINLIL_HIL_DIAG_READY node=%u peer=%u",
             (unsigned int)CONFIG_NINLIL_NODE_ID,
             (unsigned int)CONFIG_NINLIL_PEER_ID);
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
                frame.target == CONFIG_NINLIL_NODE_ID &&
                frame.source == CONFIG_NINLIL_PEER_ID) {
                ESP_LOGI(
                    TAG,
                    "DIAG type=%u seq=%lu source=%u target=%u RSSI=%d SNR=%d",
                    (unsigned int)frame.type, (unsigned long)frame.sequence,
                    (unsigned int)frame.source, (unsigned int)frame.target,
                    (int)info.rssi_dbm, (int)info.snr_db);
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
        if (now_ms() >= progress_at) {
            ESP_LOGI(TAG,
                     "DIAG_PROGRESS node=%u sent=%lu received=%lu timeout=%lu "
                     "cca_busy=%lu",
                     (unsigned int)CONFIG_NINLIL_NODE_ID, (unsigned long)sent,
                     (unsigned long)received, (unsigned long)timed_out,
                     (unsigned long)radio->channel_busy);
            progress_at = now_ms() + UINT64_C(5000);
        }
        if (now_ms() >= campaign_deadline) {
            ESP_LOGE(TAG,
                     "NINLIL_HIL_DIAG result=FAIL reason=campaign-deadline "
                     "sent=%lu received=%lu timeout=%lu",
                     (unsigned long)sent, (unsigned long)received,
                     (unsigned long)timed_out);
            return;
        }
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
#elif defined(CONFIG_NINLIL_M1_MODE_SECURE_BENCH)
    ninlil_secure_bench(&radio);
#else
    run_delivery(&radio);
#endif
    ninlil_sx1262_radio_deinit(&radio);
}
