#include "ninlil_sx1262_radio.h"
#include "ninlil_board_seeed_b2b.h"
#include "ninlil_radio.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "sx126x.h"
#include "sx126x_regs.h"

#include <string.h>

#define TCXO_STARTUP_RTC_STEPS 320u
#define RADIO_TX_GUARD_MS 500u
#define RADIO_TX_LIMIT_MS 5000u
#define NINLIL_LORA_PRIVATE_SYNC_WORD_MSB 0x14u
#define NINLIL_LORA_PRIVATE_SYNC_WORD_LSB 0x24u

static void IRAM_ATTR dio1_isr(void *context)
{
    ninlil_sx1262_radio *radio = context;
    BaseType_t awakened = pdFALSE;

    if (radio && radio->owner_task)
        vTaskNotifyGiveFromISR(radio->owner_task, &awakened);
    if (awakened == pdTRUE)
        portYIELD_FROM_ISR(awakened);
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
    uint32_t level = (uint32_t)(receive == radio->rx_gate_active_high);

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

static int apply_profile(ninlil_sx1262_radio *radio)
{
    static const uint8_t private_sync_word[2] = {
        NINLIL_LORA_PRIVATE_SYNC_WORD_MSB,
        NINLIL_LORA_PRIVATE_SYNC_WORD_LSB,
    };
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
    if (status_ok(sx126x_set_pkt_type(&radio->hal, SX126X_PKT_TYPE_LORA)) !=
            NINLIL_OK ||
        status_ok(sx126x_write_register(
            &radio->hal, SX126X_REG_LR_SYNCWORD, private_sync_word,
            (uint8_t)sizeof(private_sync_word))) != NINLIL_OK ||
        status_ok(sx126x_set_rf_freq(
            &radio->hal, radio->profile.frequency_hz)) != NINLIL_OK ||
        calibrate_image(radio) != NINLIL_OK ||
        status_ok(sx126x_set_lora_mod_params(&radio->hal, &modulation)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_lora_pkt_params(&radio->hal, &packet)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_buffer_base_address(&radio->hal, 0u, 0u)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_dio_irq_params(&radio->hal, irq_mask, irq_mask, 0u,
                                            0u)) != NINLIL_OK)
        return NINLIL_ERR_IO;
    if (radio->profile.tx_enabled) {
        sx126x_pa_cfg_params_t pa = {0x04u, 0x07u, 0x00u, 0x01u};

        if (status_ok(sx126x_set_pa_cfg(&radio->hal, &pa)) != NINLIL_OK ||
            status_ok(sx126x_set_tx_params(&radio->hal,
                                           radio->profile.tx_power_dbm,
                                           SX126X_RAMP_200_US)) != NINLIL_OK)
            return NINLIL_ERR_IO;
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
    if (radio->profile.frequency_hz == 0u) {
        radio->rx_active = false;
        return NINLIL_OK;
    }
    if (set_rx_gate(radio, true) != NINLIL_OK) {
        radio->io_errors++;
        radio->rx_active = false;
        return NINLIL_ERR_IO;
    }
    if (status_ok(sx126x_clear_irq_status(&radio->hal, SX126X_IRQ_ALL)) !=
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
    radio->owner_task = xTaskGetCurrentTaskHandle();
    radio->rx_gate_active_high = rx_gate_active_high;
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
    while (ulTaskNotifyTake(pdTRUE, 0u) != 0u) {
    }
    if (set_rx_gate(radio, false) != NINLIL_OK) {
        radio->io_errors++;
        return resume_rx(radio, NINLIL_ERR_IO);
    }
    if (status_ok(sx126x_set_lora_pkt_params(&radio->hal, &packet)) !=
            NINLIL_OK ||
        status_ok(sx126x_write_buffer(&radio->hal, 0u, data,
                                      (uint8_t)length)) != NINLIL_OK ||
        status_ok(sx126x_clear_irq_status(&radio->hal, SX126X_IRQ_ALL)) !=
            NINLIL_OK ||
        status_ok(sx126x_set_tx(&radio->hal, timeout_ms)) != NINLIL_OK) {
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
    if (ulTaskNotifyTake(pdTRUE, wait_ticks) == 0u) {
        if (status_ok(sx126x_get_irq_status(&radio->hal, &irq)) != NINLIL_OK) {
            radio->io_errors++;
            return NINLIL_ERR_IO;
        }
        if (irq == SX126X_IRQ_NONE)
            return NINLIL_ERR_EMPTY;
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
