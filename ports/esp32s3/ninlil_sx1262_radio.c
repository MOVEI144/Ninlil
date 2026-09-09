#include "ninlil_sx1262_radio.h"
#include "ninlil_board_seeed_b2b.h"
#include "ninlil_radio.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sx126x.h"
#include "sx126x_regs.h"

#include <string.h>

#define TCXO_STARTUP_RTC_STEPS 320u
#define RADIO_TX_GUARD_MS 500u
#define RADIO_TX_LIMIT_MS 5000u
#define NINLIL_LORA_PRIVATE_SYNC_WORD_MSB 0x14u
#define NINLIL_LORA_PRIVATE_SYNC_WORD_LSB 0x24u
#define JP_CCA_US INT64_C(5000)
#define JP_TX_PAUSE_US INT64_C(50000)
#define JP_MAX_AIRTIME_MS 400u
#define RX_PROGRESS (SX126X_IRQ_PREAMBLE_DETECTED | SX126X_IRQ_HEADER_VALID)
#define RX_FINISHED                                                            \
    (SX126X_IRQ_RX_DONE | SX126X_IRQ_HEADER_ERROR | SX126X_IRQ_CRC_ERROR |     \
     SX126X_IRQ_TIMEOUT)

static bool uses_jp_cca(const ninlil_sx1262_radio *radio)
{
    return radio->profile.region && strcmp(radio->profile.region, "JP") == 0;
}

static void IRAM_ATTR dio1_isr(void *context)
{
    ninlil_sx1262_radio *radio = context;
    BaseType_t awakened = pdFALSE;

    if (radio && radio->owner_task)
        vTaskNotifyGiveFromISR(radio->owner_task, &awakened);
    if (awakened == pdTRUE)
        portYIELD_FROM_ISR();
}

static int status_ok(sx126x_status_t status)
{
    return status == SX126X_STATUS_OK ? NINLIL_OK : NINLIL_ERR_IO;
}

static int caller_is_owner(const ninlil_sx1262_radio *radio)
{
    return radio && radio->owner_task == xTaskGetCurrentTaskHandle();
}

static int set_rx_gate(const ninlil_sx1262_radio *radio, bool receive);

static int configure_gpio(ninlil_sx1262_radio *radio)
{
    gpio_config_t config;
    esp_err_t rc;

    memset(&config, 0, sizeof(config));
    config.pin_bit_mask = UINT64_C(1) << NINLIL_SX1262_PIN_DIO1;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_POSEDGE;
    if (gpio_config(&config) != ESP_OK)
        return NINLIL_ERR_IO;

    memset(&config, 0, sizeof(config));
    config.pin_bit_mask = UINT64_C(1) << NINLIL_SX1262_PIN_RF_GATE;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK)
        return NINLIL_ERR_IO;

    rc = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE)
        return NINLIL_ERR_IO;
    rc = gpio_isr_handler_add((gpio_num_t)NINLIL_SX1262_PIN_DIO1, dio1_isr,
                              radio);
    if (rc != ESP_OK)
        return NINLIL_ERR_IO;
    radio->isr_installed = true;
    return set_rx_gate(radio, true);
}

static int set_rx_gate(const ninlil_sx1262_radio *radio, bool receive)
{
    bool level = receive == radio->rx_gate_active_high;

    return gpio_set_level((gpio_num_t)NINLIL_SX1262_PIN_RF_GATE, level) ==
                   ESP_OK
               ? NINLIL_OK
               : NINLIL_ERR_IO;
}

static int map_bandwidth(uint32_t bandwidth_hz, sx126x_lora_bw_t *bandwidth)
{
    switch (bandwidth_hz) {
    case 125000u:
        *bandwidth = SX126X_LORA_BW_125;
        return NINLIL_OK;
    case 250000u:
        *bandwidth = SX126X_LORA_BW_250;
        return NINLIL_OK;
    case 500000u:
        *bandwidth = SX126X_LORA_BW_500;
        return NINLIL_OK;
    default:
        return NINLIL_ERR_INVALID;
    }
}

static int calibrate_image(ninlil_sx1262_radio *radio)
{
    uint32_t frequency = radio->profile.frequency_hz;

    if (frequency >= NINLIL_SX1262_BAND_863_MIN_HZ &&
        frequency <= NINLIL_SX1262_BAND_863_MAX_HZ)
        return status_ok(sx126x_cal_img_in_mhz(&radio->hal, 863u, 870u));
    if (frequency >= NINLIL_SX1262_BAND_902_MIN_HZ &&
        frequency <= NINLIL_SX1262_BAND_902_MAX_HZ)
        return status_ok(sx126x_cal_img_in_mhz(&radio->hal, 902u, 928u));
    return NINLIL_ERR_INVALID;
}

static int map_coding_rate(uint8_t denominator, sx126x_lora_cr_t *rate)
{
    switch (denominator) {
    case 5u:
        *rate = SX126X_LORA_CR_4_5;
        return NINLIL_OK;
    case 6u:
        *rate = SX126X_LORA_CR_4_6;
        return NINLIL_OK;
    case 7u:
        *rate = SX126X_LORA_CR_4_7;
        return NINLIL_OK;
    case 8u:
        *rate = SX126X_LORA_CR_4_8;
        return NINLIL_OK;
    default:
        return NINLIL_ERR_INVALID;
    }
}

static int build_lora_parameters(const ninlil_rf_profile *profile,
                                 uint8_t payload_length,
                                 sx126x_mod_params_lora_t *modulation,
                                 sx126x_pkt_params_lora_t *packet)
{
    sx126x_lora_bw_t bandwidth;
    sx126x_lora_cr_t coding_rate;
    uint32_t symbol_us;

    if (map_bandwidth(profile->bandwidth_hz, &bandwidth) != NINLIL_OK ||
        map_coding_rate(profile->coding_rate_denominator, &coding_rate) !=
            NINLIL_OK)
        return NINLIL_ERR_INVALID;
    memset(modulation, 0, sizeof(*modulation));
    modulation->sf = (sx126x_lora_sf_t)profile->spreading_factor;
    modulation->bw = bandwidth;
    modulation->cr = coding_rate;
    symbol_us = ((uint32_t)1u << profile->spreading_factor) *
                UINT32_C(1000000) / profile->bandwidth_hz;
    modulation->ldro = symbol_us >= 16000u ? 1u : 0u;

    memset(packet, 0, sizeof(*packet));
    packet->preamble_len_in_symb = profile->preamble_symbols;
    packet->header_type = SX126X_LORA_PKT_EXPLICIT;
    packet->pld_len_in_bytes = payload_length;
    packet->crc_is_on = true;
    packet->invert_iq_is_on = false;
    return NINLIL_OK;
}

static int lora_modem(ninlil_sx1262_radio *radio)
{
    static const uint8_t private_sync_word[2] = {
        NINLIL_LORA_PRIVATE_SYNC_WORD_MSB,
        NINLIL_LORA_PRIVATE_SYNC_WORD_LSB,
    };
    return status_ok(sx126x_set_pkt_type(&radio->hal, SX126X_PKT_TYPE_LORA)) ==
                       NINLIL_OK &&
                   status_ok(sx126x_write_register(
                       &radio->hal, SX126X_REG_LR_SYNCWORD, private_sync_word,
                       (uint8_t)sizeof(private_sync_word))) == NINLIL_OK
               ? NINLIL_OK
               : NINLIL_ERR_IO;
}
static int apply_profile(ninlil_sx1262_radio *radio)
{
    sx126x_mod_params_lora_t modulation;
    sx126x_pkt_params_lora_t packet;
    sx126x_irq_mask_t irq_mask = SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE |
                                 SX126X_IRQ_HEADER_ERROR |
                                 SX126X_IRQ_CRC_ERROR | SX126X_IRQ_TIMEOUT;

    if (radio->profile.frequency_hz == 0u)
        return NINLIL_OK;
    if (build_lora_parameters(&radio->profile, NINLIL_RADIO_MTU, &modulation,
                              &packet) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    if (lora_modem(radio) != NINLIL_OK ||
        status_ok(sx126x_set_rf_freq(
            &radio->hal, radio->profile.frequency_hz)) != NINLIL_OK ||
        calibrate_image(radio) != NINLIL_OK ||
        status_ok(sx126x_set_lora_mod_params(&radio->hal, &modulation)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_lora_pkt_params(&radio->hal, &packet)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_buffer_base_address(&radio->hal, 0u, 0u)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_dio_irq_params(&radio->hal, irq_mask | RX_PROGRESS,
                                            irq_mask, 0u, 0u)) != NINLIL_OK)
        return NINLIL_ERR_IO;
    if (radio->profile.tx_enabled) {
        sx126x_pa_cfg_params_t pa = {0x04u, 0x07u, 0x00u, 0x01u};

        if (status_ok(sx126x_set_pa_cfg(&radio->hal, &pa)) != NINLIL_OK ||
            status_ok(sx126x_set_tx_params(&radio->hal,
                                           radio->profile.tx_power_dbm,
                                           SX126X_RAMP_200_US)) != NINLIL_OK)
            return NINLIL_ERR_IO;
        radio->applied_power_dbm = radio->profile.tx_power_dbm;
    }
    return NINLIL_OK;
}

static int configure_radio(ninlil_sx1262_radio *radio)
{
    if (status_ok(sx126x_reset(&radio->hal)) != NINLIL_OK ||
        status_ok(sx126x_set_standby(&radio->hal, SX126X_STANDBY_CFG_RC)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_reg_mode(&radio->hal, SX126X_REG_MODE_DCDC)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_dio3_as_tcxo_ctrl(
            &radio->hal, SX126X_TCXO_CTRL_1_8V, TCXO_STARTUP_RTC_STEPS)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_dio2_as_rf_sw_ctrl(&radio->hal, true)) !=
            NINLIL_OK ||
        status_ok(sx126x_cal(&radio->hal, SX126X_CAL_ALL)) != NINLIL_OK ||
        status_ok(sx126x_set_rx_tx_fallback_mode(
            &radio->hal, SX126X_FALLBACK_STDBY_RC)) != NINLIL_OK ||
        apply_profile(radio) != NINLIL_OK) {
        radio->io_errors++;
        radio->configured = false;
        radio->rx_active = false;
        return NINLIL_ERR_IO;
    }
    radio->configured = true;
    radio->rx_active = false;
    return NINLIL_OK;
}

static int start_rx(ninlil_sx1262_radio *radio)
{
    sx126x_mod_params_lora_t modulation;
    sx126x_pkt_params_lora_t packet;
    radio->rx_deadline_us = 0;
    if (radio->profile.frequency_hz == 0u) {
        radio->rx_active = false;
        return NINLIL_OK;
    }
    if (set_rx_gate(radio, true) != NINLIL_OK) {
        radio->io_errors++;
        radio->rx_active = false;
        return NINLIL_ERR_IO;
    }
    /* TX programs its own length. Explicit-header RX uses the same field as

     * its accepted maximum, so restore the physical MTU before listening. */
    if (build_lora_parameters(&radio->profile, NINLIL_RADIO_MTU, &modulation,
                              &packet) != NINLIL_OK ||
        status_ok(sx126x_set_standby(&radio->hal, SX126X_STANDBY_CFG_RC)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_lora_pkt_params(&radio->hal, &packet)) !=
            NINLIL_OK ||
        status_ok(sx126x_clear_irq_status(&radio->hal, SX126X_IRQ_ALL)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_rx_with_timeout_in_rtc_step(
            &radio->hal, SX126X_RX_CONTINUOUS)) != NINLIL_OK) {
        radio->io_errors++;
        radio->rx_active = false;
        return NINLIL_ERR_IO;
    }
    radio->rx_active = true;
    return NINLIL_OK;
}

static int resume_rx(ninlil_sx1262_radio *radio, int operation_result)
{
    int rx_result = start_rx(radio);

    return rx_result == NINLIL_OK ? operation_result : rx_result;
}

/* Keep receiving an observed preamble/header instead of destroying the packet

 * to sense the channel for a queued TX. False preambles have a bounded
 * lifetime. */
static int receive_progress(ninlil_sx1262_radio *radio)
{
    int64_t now = esp_timer_get_time();
    if (now < 0 || now > INT64_MAX - INT64_C(5000000))
        return NINLIL_ERR_IO;
    if (!radio->rx_deadline_us) {
        sx126x_mod_params_lora_t modulation;
        sx126x_pkt_params_lora_t packet;
        uint32_t duration;
        if (build_lora_parameters(&radio->profile, NINLIL_RADIO_MTU,
                                  &modulation, &packet) != NINLIL_OK)
            return NINLIL_ERR_INVALID;
        duration = sx126x_get_lora_time_on_air_in_ms(&packet, &modulation);
        if (!duration || duration > RADIO_TX_LIMIT_MS - RADIO_TX_GUARD_MS)
            duration = RADIO_TX_LIMIT_MS - RADIO_TX_GUARD_MS;
        radio->rx_deadline_us =
            now + (int64_t)(duration + RADIO_TX_GUARD_MS) * 1000;
    }
    if (now < radio->rx_deadline_us)
        return NINLIL_ERR_BUSY;
    radio->timeouts++;
    return resume_rx(radio, NINLIL_ERR_TIMEOUT);
}

static int check_jp_channel(ninlil_sx1262_radio *radio,
                            const sx126x_mod_params_lora_t *modulation)
{
    const sx126x_mod_params_gfsk_t sensing = {
        .br_in_bps = 50000u,
        .fdev_in_hz = 25000u,
        .pulse_shape = SX126X_GFSK_PULSE_SHAPE_OFF,
        .bw_dsb_param = SX126X_GFSK_BW_234300};
    const sx126x_pkt_params_gfsk_t packet = {
        .preamble_len_in_bits = 8u,
        .preamble_detector = SX126X_GFSK_PREAMBLE_DETECTOR_OFF,
        .address_filtering = SX126X_GFSK_ADDRESS_FILTERING_DISABLE,
        .header_type = SX126X_GFSK_PKT_VAR_LEN,
        .pld_len_in_bytes = 255u,
        .crc_type = SX126X_GFSK_CRC_OFF,
        .dc_free = SX126X_GFSK_DC_FREE_OFF};
    sx126x_chip_status_t status;
    unsigned int sample;
    int64_t start;
    int result = NINLIL_ERR_IO;

    /* Semtech's channel-free procedure uses GFSK RSSI sensing. The 234.3 kHz

     * double-sided receive filter covers the whole 200 kHz unit channel.

     * Only sensing changes modem; every transmission remains the fixed LoRa

     * profile, with its sync word restored after switching packet type. */
    radio->cca_stage = 1u;
    radio->cca_chip_mode = 0u;
    radio->cca_cmd_status = 0u;
    radio->cca_rssi_dbm = 0;
    radio->rx_active = false;
    if (sx126x_set_standby(&radio->hal, SX126X_STANDBY_CFG_RC) !=
            SX126X_STATUS_OK ||
        sx126x_set_pkt_type(&radio->hal, SX126X_PKT_TYPE_GFSK) !=
            SX126X_STATUS_OK ||
        sx126x_set_gfsk_mod_params(&radio->hal, &sensing) != SX126X_STATUS_OK ||
        sx126x_set_gfsk_pkt_params(&radio->hal, &packet) != SX126X_STATUS_OK ||
        set_rx_gate(radio, true) != NINLIL_OK ||
        sx126x_set_rx_with_timeout_in_rtc_step(
            &radio->hal, SX126X_RX_CONTINUOUS) != SX126X_STATUS_OK)
        goto restore;
    esp_rom_delay_us(1000u);
    radio->cca_stage = 2u;
    if (sx126x_get_status(&radio->hal, &status) != SX126X_STATUS_OK)
        goto restore;
    radio->cca_chip_mode = (uint8_t)status.chip_mode;
    radio->cca_cmd_status = (uint8_t)status.cmd_status;
    if (status.chip_mode != SX126X_CHIP_MODE_RX ||
        (status.cmd_status != SX126X_CMD_STATUS_RFU &&
         status.cmd_status != SX126X_CMD_STATUS_DATA_AVAILABLE &&
         status.cmd_status != SX126X_CMD_STATUS_CMD_TX_DONE))
        goto restore;
    // RFU=1 is observed in RX on this silicon and is not command-success
    // evidence. CCA still requires RX mode and independent RSSI readings;
    // TX completion later requires its own TX_DONE IRQ.
    start = esp_timer_get_time();
    if (start < 0)
        goto restore;
    radio->cca_stage = 3u;
    // A stalled/backward clock or repeated busy channel cannot hold this call.
    for (sample = 0u; sample < 100u; sample++) {
        int16_t rssi = 0;
        int64_t now;

        if (sx126x_get_rssi_inst(&radio->hal, &rssi) != SX126X_STATUS_OK)
            break;
        radio->cca_rssi_dbm = rssi;
        // Semtech rounds raw 255 down to -128 dBm (-raw >> 1).
        // This is the representable noise floor, not an I/O error sentinel.
        if (rssi < -128 || rssi > 0)
            break;
        if (rssi >= -80) {
            radio->channel_busy++;
            result = NINLIL_ERR_BUSY;
            break;
        }
        now = esp_timer_get_time();
        if (now < start)
            break;
        if (now - start >= JP_CCA_US) {
            result = NINLIL_OK;
            break;
        }
        esp_rom_delay_us(200u);
    }
restore:
    if (sx126x_set_standby(&radio->hal, SX126X_STANDBY_CFG_RC) !=
            SX126X_STATUS_OK ||
        lora_modem(radio) != NINLIL_OK ||
        sx126x_set_lora_mod_params(&radio->hal, modulation) !=
            SX126X_STATUS_OK) {
        radio->configured = false;
        radio->cca_stage = 4u;
        return NINLIL_ERR_IO;
    }
    if (result != NINLIL_OK)
        return resume_rx(radio, result);
    radio->cca_stage = 5u;
    return NINLIL_OK;
}

int ninlil_sx1262_radio_init(ninlil_sx1262_radio *radio,
                             const ninlil_rf_profile *profile,
                             bool rx_gate_active_high)
{
    int rc;

    if (!radio || !profile || ninlil_rf_profile_validate(profile) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    if (profile->frequency_hz != 0u &&
        !((profile->frequency_hz >= NINLIL_SX1262_BAND_863_MIN_HZ &&
           profile->frequency_hz <= NINLIL_SX1262_BAND_863_MAX_HZ) ||
          (profile->frequency_hz >= NINLIL_SX1262_BAND_902_MIN_HZ &&
           profile->frequency_hz <= NINLIL_SX1262_BAND_902_MAX_HZ)))
        return NINLIL_ERR_INVALID;
    memset(radio, 0, sizeof(*radio));
    radio->profile = *profile;
    radio->requested_power_dbm = radio->applied_power_dbm =
        profile->tx_power_dbm;
    radio->owner_task = xTaskGetCurrentTaskHandle();
    radio->rx_gate_active_high = rx_gate_active_high;
    if (uses_jp_cca(radio)) {
        int64_t now = esp_timer_get_time();

        if (now < 0 || now > INT64_MAX - JP_TX_PAUSE_US)
            return NINLIL_ERR_IO;
        radio->tx_not_before_us = now + JP_TX_PAUSE_US;
    }
    if (!radio->owner_task || ninlil_sx1262_hal_init(&radio->hal) != 0)
        return NINLIL_ERR_IO;
    rc = configure_gpio(radio);
    if (rc == NINLIL_OK)
        rc = configure_radio(radio);
    if (rc == NINLIL_OK)
        rc = start_rx(radio);
    if (rc != NINLIL_OK)
        ninlil_sx1262_radio_deinit(radio);
    return rc;
}

void ninlil_sx1262_radio_deinit(ninlil_sx1262_radio *radio)
{
    if (!radio || (radio->owner_task && !caller_is_owner(radio)))
        return;
    if (radio->isr_installed) {
        (void)gpio_isr_handler_remove((gpio_num_t)NINLIL_SX1262_PIN_DIO1);
        radio->isr_installed = false;
    }
    (void)set_rx_gate(radio, true);
    ninlil_sx1262_hal_deinit(&radio->hal);
    radio->configured = false;
    radio->rx_active = false;
    radio->owner_task = NULL;
}

int ninlil_sx1262_radio_recover(ninlil_sx1262_radio *radio)
{
    int rc;

    if (!caller_is_owner(radio))
        return NINLIL_ERR_INVALID;
    rc = configure_radio(radio);
    if (rc == NINLIL_OK)
        rc = start_rx(radio);
    return rc;
}

int ninlil_sx1262_radio_sleep(ninlil_sx1262_radio *radio)
{
    if (!caller_is_owner(radio) || !radio->configured)
        return NINLIL_ERR_STATE;
    /* The synchronous owner has completed TX. An unfinished RX has not been
     * acknowledged and remains owned by its sender. Preserve the TX pause. */
    radio->configured = radio->rx_active = false;
    return sx126x_set_standby(&radio->hal, SX126X_STANDBY_CFG_RC) ==
                       SX126X_STATUS_OK &&
                   sx126x_set_sleep(&radio->hal, SX126X_SLEEP_CFG_WARM_START) ==
                       SX126X_STATUS_OK
               ? NINLIL_OK
               : NINLIL_ERR_IO;
}

int ninlil_sx1262_radio_airtime(const ninlil_sx1262_radio *radio,
                                uint16_t length, uint32_t *airtime_us)
{
    sx126x_mod_params_lora_t modulation;
    sx126x_pkt_params_lora_t packet;
    uint32_t ms;
    if (!radio || !airtime_us || !length || length > NINLIL_RADIO_MTU ||
        build_lora_parameters(&radio->profile, (uint8_t)length, &modulation,
                              &packet) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    ms = sx126x_get_lora_time_on_air_in_ms(&packet, &modulation);
    if (!ms || ms > 400u)
        return NINLIL_ERR_TOO_LARGE;
    *airtime_us = ms * 1000u;
    return NINLIL_OK;
}

int ninlil_sx1262_radio_power(ninlil_sx1262_radio *radio, int8_t power)
{
    if (!caller_is_owner(radio) || !radio->configured || power < -9 ||
        power > radio->profile.tx_power_dbm)
        return NINLIL_ERR_INVALID;
    radio->requested_power_dbm = power;
    return NINLIL_OK;
}

int ninlil_sx1262_radio_send(ninlil_sx1262_radio *radio, const uint8_t *data,
                             uint16_t length)
{
    sx126x_mod_params_lora_t modulation;
    sx126x_pkt_params_lora_t packet;
    sx126x_irq_mask_t irq = SX126X_IRQ_NONE;
    uint32_t time_on_air_ms;
    uint32_t timeout_ms;
    int rc = NINLIL_OK;

    if (!caller_is_owner(radio) || !data || length == 0u)
        return NINLIL_ERR_INVALID;
    if (length > NINLIL_RADIO_MTU)
        return NINLIL_ERR_TOO_LARGE;
    if (!radio->profile.tx_enabled || !radio->configured ||
        radio->profile.frequency_hz == 0u)
        return NINLIL_ERR_FAULT;
    if (build_lora_parameters(&radio->profile, (uint8_t)length, &modulation,
                              &packet) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    time_on_air_ms = sx126x_get_lora_time_on_air_in_ms(&packet, &modulation);
    timeout_ms = time_on_air_ms + RADIO_TX_GUARD_MS;
    if (timeout_ms > RADIO_TX_LIMIT_MS)
        return NINLIL_ERR_TOO_LARGE;
    if (uses_jp_cca(radio)) {
        int64_t now = esp_timer_get_time();

        if (now < 0 || now > INT64_MAX - INT64_C(1000000))
            return NINLIL_ERR_IO;
        if (time_on_air_ms > JP_MAX_AIRTIME_MS)
            return NINLIL_ERR_TOO_LARGE;
        if (now < radio->tx_not_before_us)
            return NINLIL_ERR_BUSY;
    }

    if (status_ok(sx126x_get_irq_status(&radio->hal, &irq)) != NINLIL_OK)
        return NINLIL_ERR_IO;
    if (irq & RX_FINISHED)
        return NINLIL_ERR_BUSY;
    if (irq & RX_PROGRESS)
        return receive_progress(radio);
    radio->rx_active = false;
    if (status_ok(sx126x_set_standby(&radio->hal, SX126X_STANDBY_CFG_RC)) !=
        NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    if (status_ok(sx126x_get_irq_status(&radio->hal, &irq)) != NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    if ((irq & (SX126X_IRQ_RX_DONE | SX126X_IRQ_CRC_ERROR |
                SX126X_IRQ_HEADER_ERROR | SX126X_IRQ_TIMEOUT)) != 0u) {
        radio->rx_active = true;
        return NINLIL_ERR_BUSY;
    }
    if (uses_jp_cca(radio)) {
        rc = check_jp_channel(radio, &modulation);
        if (rc != NINLIL_OK)
            return rc;
    }
    if (set_rx_gate(radio, false) != NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    if (status_ok(sx126x_set_tx_params(&radio->hal, radio->requested_power_dbm,
                                       SX126X_RAMP_200_US)) != NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    radio->applied_power_dbm = radio->requested_power_dbm;
    if (status_ok(sx126x_set_lora_pkt_params(&radio->hal, &packet)) !=
            NINLIL_OK ||
        status_ok(sx126x_write_buffer(&radio->hal, 0u, data,
                                      (uint8_t)length)) != NINLIL_OK ||
        status_ok(sx126x_clear_irq_status(&radio->hal, SX126X_IRQ_ALL)) !=
            NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    if (uses_jp_cca(radio)) {
        int64_t now = esp_timer_get_time();

        if (now < 0 || now > INT64_MAX - INT64_C(1000000))
            return resume_rx(radio, NINLIL_ERR_IO);
        // Reserve the whole possible TX interval before its external effect.
        radio->tx_not_before_us =
            now + (int64_t)timeout_ms * 1000 + JP_TX_PAUSE_US;
    }
    // CCA may have raised a receive notification. Clear it in standby after
    // clearing IRQ flags, so it cannot masquerade as completion of this TX.
    (void)ulTaskNotifyTake(pdTRUE, 0u);
    if (status_ok(sx126x_set_tx(&radio->hal, timeout_ms)) != NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) == 0u) {
        radio->timeouts++;
        return resume_rx(radio, NINLIL_ERR_TIMEOUT);
    }
    if (status_ok(sx126x_get_and_clear_irq_status(&radio->hal, &irq)) !=
        NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    if ((irq & SX126X_IRQ_TX_DONE) == 0u) {
        if ((irq & SX126X_IRQ_TIMEOUT) != 0u) {
            radio->timeouts++;
            rc = NINLIL_ERR_TIMEOUT;
        } else {
            radio->io_errors++;
            rc = NINLIL_ERR_IO;
        }
    }
    if (uses_jp_cca(radio) && rc == NINLIL_OK) {
        int64_t now = esp_timer_get_time();

        if (now < 0 || now > INT64_MAX - JP_TX_PAUSE_US)
            return resume_rx(radio, NINLIL_ERR_IO);
        radio->tx_not_before_us = now + JP_TX_PAUSE_US;
    }
    return resume_rx(radio, rc);
}

int ninlil_sx1262_radio_receive(ninlil_sx1262_radio *radio, uint8_t *data,
                                uint16_t capacity, uint16_t *length,
                                ninlil_sx1262_rx_info *info,
                                TickType_t wait_ticks)
{
    sx126x_irq_mask_t irq = SX126X_IRQ_NONE;
    sx126x_rx_buffer_status_t buffer_status;
    sx126x_pkt_status_lora_t packet_status;
    int rc;

    if (!caller_is_owner(radio) || !data || !length || capacity == 0u)
        return NINLIL_ERR_INVALID;
    *length = 0u;
    if (!radio->rx_active)
        return NINLIL_ERR_BUSY;
    (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
    if (status_ok(sx126x_get_irq_status(&radio->hal, &irq)) != NINLIL_OK) {
        radio->io_errors++;
        return NINLIL_ERR_IO;
    }
    if (!(irq & RX_FINISHED)) {
        if (!(irq & RX_PROGRESS))
            return NINLIL_ERR_EMPTY;
        rc = receive_progress(radio);
        return rc == NINLIL_ERR_BUSY ? NINLIL_ERR_EMPTY : rc;
    }
    if (status_ok(sx126x_get_and_clear_irq_status(&radio->hal, &irq)) !=
        NINLIL_OK) {
        radio->io_errors++;
        return NINLIL_ERR_IO;
    }
    if ((irq & SX126X_IRQ_CRC_ERROR) != 0u) {
        radio->crc_errors++;
        return resume_rx(radio, NINLIL_ERR_INVALID);
    }
    if ((irq & SX126X_IRQ_HEADER_ERROR) != 0u) {
        radio->header_errors++;
        return resume_rx(radio, NINLIL_ERR_INVALID);
    }
    if ((irq & SX126X_IRQ_TIMEOUT) != 0u) {
        radio->timeouts++;
        return resume_rx(radio, NINLIL_ERR_TIMEOUT);
    }
    if ((irq & SX126X_IRQ_RX_DONE) == 0u)
        return NINLIL_ERR_EMPTY;
    if (status_ok(sx126x_get_rx_buffer_status(&radio->hal, &buffer_status)) !=
        NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    if (buffer_status.pld_len_in_bytes > capacity ||
        buffer_status.pld_len_in_bytes > NINLIL_RADIO_MTU)
        return resume_rx(radio, NINLIL_ERR_TOO_LARGE);
    if (status_ok(sx126x_read_buffer(
            &radio->hal, buffer_status.buffer_start_pointer, data,
            buffer_status.pld_len_in_bytes)) != NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    *length = buffer_status.pld_len_in_bytes;
    if (info) {
        memset(info, 0, sizeof(*info));
        if (status_ok(sx126x_get_lora_pkt_status(
                &radio->hal, &packet_status)) == NINLIL_OK) {
            info->rssi_dbm = packet_status.rssi_pkt_in_dbm;
            info->snr_db = packet_status.snr_pkt_in_db;
        }
    }
    rc = start_rx(radio);
    return rc == NINLIL_OK ? NINLIL_OK : rc;
}
