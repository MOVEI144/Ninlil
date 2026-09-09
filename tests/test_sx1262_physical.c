#include "ninlil_board_seeed_b2b.h"
#include "ninlil_radio.h"
#include "ninlil_sx1262_radio.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sx126x.h"
#include "sx126x_regs.h"

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

static int task_a_token;
static int task_b_token;
static int fake_spi_token;
static TaskHandle_t fake_current_task;
static uint32_t fake_notifications;
static void (*fake_isr)(void *);
static void *fake_isr_context;
static sx126x_irq_mask_t fake_irq_status;
static sx126x_irq_mask_t fake_tx_completion_irq;
static uint8_t fake_rx_data[NINLIL_RADIO_MTU];
static uint8_t fake_rx_length;
static uint8_t fake_payload_limit;
static bool fake_enforce_payload_limit;
static bool fake_tx_notifies;
static bool fake_fail_hal_init;
static bool fake_fail_standby;
static bool fake_fail_gpio_config;
static bool fake_fail_gpio_set;
static bool fake_fail_get_irq;
static unsigned int fake_reset_calls;
static unsigned int fake_start_rx_calls;
static unsigned int fake_set_tx_calls;
static unsigned int fake_hal_deinit_calls;
static unsigned int fake_gpio_remove_calls;
static unsigned int fake_dio3_order;
static unsigned int fake_dio2_order;
static unsigned int fake_call_order;
static unsigned int fake_register_writes;
static uint16_t fake_register_address;
static uint8_t fake_register_data[2];
static uint16_t fake_cal_low_mhz;
static uint16_t fake_cal_high_mhz;
static uint32_t fake_last_tx_timeout_ms;
static int fake_levels[64];
static int64_t fake_time_us;
static bool fake_clock_stalled;
static bool fake_bad_chip_status;
static int fake_cmd_status;
static int16_t fake_rssi;
static unsigned int fake_rssi_calls;
static unsigned int fake_busy_sample;
static unsigned int fake_rssi_error_sample;
static uint32_t fake_airtime_ms;
static sx126x_lora_bw_t fake_bandwidth;
static int fake_packet_type;
static bool fake_sync_restored;
static unsigned int fake_modem_failure;
static int8_t fake_power;
static bool fake_power_failure;

int64_t esp_timer_get_time(void)
{
    return fake_time_us;
}

void esp_rom_delay_us(uint32_t us)
{
    if (!fake_clock_stalled)
        fake_time_us += us;
}

sx126x_status_t sx126x_get_status(const void *context,
                                  sx126x_chip_status_t *status)
{
    (void)context;
    status->chip_mode = fake_bad_chip_status ? 7 : SX126X_CHIP_MODE_RX;
    status->cmd_status = fake_cmd_status;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_get_rssi_inst(const void *context, int16_t *rssi)
{
    (void)context;
    fake_rssi_calls++;
    fake_notifications = 1u;
    if (fake_packet_type != SX126X_PKT_TYPE_GFSK ||
        fake_bandwidth != SX126X_GFSK_BW_234300 ||
        fake_rssi_calls == fake_rssi_error_sample)
        return ESP_FAIL;
    *rssi = fake_rssi_calls == fake_busy_sample ? -80 : fake_rssi;
    return SX126X_STATUS_OK;
}

static void reset_fakes(void)
{
    fake_current_task = &task_a_token;
    fake_notifications = 0u;
    fake_isr = NULL;
    fake_isr_context = NULL;
    fake_irq_status = SX126X_IRQ_NONE;
    fake_tx_completion_irq = SX126X_IRQ_NONE;
    memset(fake_rx_data, 0, sizeof(fake_rx_data));
    fake_rx_length = 0u;
    fake_payload_limit = 0u;
    fake_enforce_payload_limit = false;
    fake_tx_notifies = false;
    fake_fail_hal_init = false;
    fake_fail_standby = false;
    fake_fail_gpio_config = false;
    fake_fail_gpio_set = false;
    fake_fail_get_irq = false;
    fake_reset_calls = 0u;
    fake_start_rx_calls = 0u;
    fake_set_tx_calls = 0u;
    fake_hal_deinit_calls = 0u;
    fake_gpio_remove_calls = 0u;
    fake_dio3_order = 0u;
    fake_dio2_order = 0u;
    fake_call_order = 0u;
    fake_register_writes = 0u;
    fake_register_address = 0u;
    memset(fake_register_data, 0, sizeof(fake_register_data));
    fake_cal_low_mhz = 0u;
    fake_cal_high_mhz = 0u;
    fake_last_tx_timeout_ms = 0u;
    memset(fake_levels, 0, sizeof(fake_levels));
    fake_time_us = 0;
    fake_clock_stalled = false;
    fake_bad_chip_status = false;
    fake_cmd_status = SX126X_CMD_STATUS_RFU;
    fake_rssi = -100;
    fake_rssi_calls = 0u;
    fake_busy_sample = 0u;
    fake_rssi_error_sample = 0u;
    fake_airtime_ms = 100u;
    fake_bandwidth = SX126X_LORA_BW_125;
    fake_modem_failure = 0u;
}

static ninlil_rf_profile make_profile(bool tx_enabled)
{
    ninlil_rf_profile profile;

    memset(&profile, 0, sizeof(profile));
    profile.tx_enabled = tx_enabled;
    profile.rf_gate_polarity_confirmed = tx_enabled;
    profile.region = tx_enabled ? "HIL" : NULL;
    profile.frequency_hz = UINT32_C(920000000);
    profile.tx_power_dbm = -9;
    profile.spreading_factor = 9u;
    profile.bandwidth_hz = UINT32_C(125000);
    profile.coding_rate_denominator = 5u;
    profile.preamble_symbols = 8u;
    return profile;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return fake_current_task;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return 4096u;
}

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait)
{
    uint32_t result;

    (void)wait;
    if (fake_notifications == 0u)
        return 0u;
    result = fake_notifications;
    if (clear == pdTRUE)
        fake_notifications = 0u;
    else
        fake_notifications--;
    return result;
}

void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *awakened)
{
    if (task == &task_a_token)
        fake_notifications++;
    if (awakened)
        *awakened = pdFALSE;
}

TickType_t xTaskGetTickCount(void)
{
    return 0u;
}

void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
}

esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    return fake_fail_gpio_config ? ESP_FAIL : ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio, int level)
{
    if (fake_fail_gpio_set)
        return ESP_FAIL;
    if (gpio >= 0 &&
        (size_t)gpio < sizeof(fake_levels) / sizeof(fake_levels[0]))
        fake_levels[gpio] = level;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio)
{
    if (gpio >= 0 &&
        (size_t)gpio < sizeof(fake_levels) / sizeof(fake_levels[0]))
        return fake_levels[gpio];
    return 0;
}

esp_err_t gpio_install_isr_service(int flags)
{
    (void)flags;
    return ESP_OK;
}

esp_err_t gpio_isr_handler_add(gpio_num_t gpio, void (*handler)(void *),
                               void *context)
{
    if (gpio != NINLIL_SX1262_PIN_DIO1)
        return ESP_FAIL;
    fake_isr = handler;
    fake_isr_context = context;
    return ESP_OK;
}

esp_err_t gpio_isr_handler_remove(gpio_num_t gpio)
{
    if (gpio != NINLIL_SX1262_PIN_DIO1)
        return ESP_FAIL;
    fake_gpio_remove_calls++;
    fake_isr = NULL;
    fake_isr_context = NULL;
    return ESP_OK;
}

int ninlil_sx1262_hal_init(ninlil_sx1262_hal_context *context)
{
    if (fake_fail_hal_init)
        return -1;
    memset(context, 0, sizeof(*context));
    context->spi = &fake_spi_token;
    context->nss = NINLIL_SX1262_PIN_NSS;
    context->reset = NINLIL_SX1262_PIN_RESET;
    context->busy = NINLIL_SX1262_PIN_BUSY;
    context->busy_timeout_ms = 20u;
    context->bus_initialized = true;
    return 0;
}

void ninlil_sx1262_hal_deinit(ninlil_sx1262_hal_context *context)
{
    fake_hal_deinit_calls++;
    context->spi = NULL;
    context->bus_initialized = false;
}

sx126x_status_t sx126x_reset(const void *context)
{
    (void)context;
    fake_reset_calls++;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_standby(const void *context, int config)
{
    (void)context;
    (void)config;
    return fake_fail_standby ? ESP_FAIL : SX126X_STATUS_OK;
}
sx126x_status_t sx126x_set_sleep(const void *context, int config)
{
    (void)context;
    return config == SX126X_SLEEP_CFG_WARM_START ? SX126X_STATUS_OK : ESP_FAIL;
}

sx126x_status_t sx126x_set_reg_mode(const void *context, int mode)
{
    (void)context;
    (void)mode;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_dio2_as_rf_sw_ctrl(const void *context, bool enable)
{
    (void)context;
    (void)enable;
    fake_dio2_order = ++fake_call_order;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_dio3_as_tcxo_ctrl(const void *context, int voltage,
                                             uint32_t timeout)
{
    (void)context;
    (void)voltage;
    (void)timeout;
    fake_dio3_order = ++fake_call_order;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_cal(const void *context, uint8_t mask)
{
    (void)context;
    (void)mask;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_cal_img_in_mhz(const void *context, uint16_t low_mhz,
                                      uint16_t high_mhz)
{
    (void)context;
    fake_cal_low_mhz = low_mhz;
    fake_cal_high_mhz = high_mhz;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_rx_tx_fallback_mode(const void *context, int mode)
{
    (void)context;
    (void)mode;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_pkt_type(const void *context, int type)
{
    (void)context;
    if ((type == SX126X_PKT_TYPE_GFSK && fake_modem_failure == 1u) ||
        (type == SX126X_PKT_TYPE_LORA && fake_modem_failure == 4u))
        return ESP_FAIL;
    fake_packet_type = type;
    fake_sync_restored = false;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_write_register(const void *context, uint16_t address,
                                      const uint8_t *data, uint8_t length)
{
    (void)context;
    if (!data || length != 2u || fake_modem_failure == 5u)
        return ESP_FAIL;
    fake_register_writes++;
    fake_register_address = address;
    memcpy(fake_register_data, data, 2u);
    fake_sync_restored = true;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_rf_freq(const void *context, uint32_t frequency)
{
    (void)context;
    (void)frequency;
    return SX126X_STATUS_OK;
}
sx126x_status_t sx126x_set_gfsk_mod_params(const void *ctx,
                                           const sx126x_mod_params_gfsk_t *p)
{
    (void)ctx;
    if (fake_modem_failure == 2u || fake_packet_type != SX126X_PKT_TYPE_GFSK ||
        p->bw_dsb_param != SX126X_GFSK_BW_234300)
        return ESP_FAIL;
    fake_bandwidth = p->bw_dsb_param;
    return SX126X_STATUS_OK;
}
sx126x_status_t sx126x_set_gfsk_pkt_params(const void *ctx,
                                           const sx126x_pkt_params_gfsk_t *p)
{
    (void)ctx;
    return fake_modem_failure != 3u &&
                   fake_packet_type == SX126X_PKT_TYPE_GFSK &&
                   p->crc_type == SX126X_GFSK_CRC_OFF
               ? SX126X_STATUS_OK
               : ESP_FAIL;
}

sx126x_status_t
sx126x_set_lora_mod_params(const void *context,
                           const sx126x_mod_params_lora_t *params)
{
    (void)context;
    fake_bandwidth = params->bw;
    return SX126X_STATUS_OK;
}

sx126x_status_t
sx126x_set_lora_pkt_params(const void *context,
                           const sx126x_pkt_params_lora_t *params)
{
    (void)context;
    fake_payload_limit = params->pld_len_in_bytes;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_buffer_base_address(const void *context, uint8_t tx,
                                               uint8_t rx)
{
    (void)context;
    (void)tx;
    (void)rx;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_dio_irq_params(const void *context, uint16_t mask,
                                          uint16_t dio1, uint16_t dio2,
                                          uint16_t dio3)
{
    (void)context;
    (void)mask;
    (void)dio1;
    (void)dio2;
    (void)dio3;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_pa_cfg(const void *context,
                                  const sx126x_pa_cfg_params_t *params)
{
    (void)context;
    (void)params;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_tx_params(const void *context, int8_t power,
                                     int ramp)
{
    (void)context;
    (void)ramp;
    if (fake_power_failure)
        return -1;
    fake_power = power;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_rx_with_timeout_in_rtc_step(const void *context,
                                                       uint32_t timeout)
{
    (void)context;
    (void)timeout;
    fake_start_rx_calls++;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_set_tx(const void *context, uint32_t timeout_ms)
{
    if (fake_packet_type != SX126X_PKT_TYPE_LORA || !fake_sync_restored)
        return ESP_FAIL;
    (void)context;
    if (fake_notifications != 0u)
        return ESP_FAIL;
    fake_set_tx_calls++;
    fake_last_tx_timeout_ms = timeout_ms;
    fake_irq_status = fake_tx_completion_irq;
    if (fake_tx_notifies)
        fake_notifications++;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_write_buffer(const void *context, uint8_t offset,
                                    const uint8_t *data, uint8_t length)
{
    (void)context;
    (void)offset;
    (void)data;
    (void)length;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_clear_irq_status(const void *context,
                                        sx126x_irq_mask_t mask)
{
    (void)context;
    fake_irq_status = (sx126x_irq_mask_t)(fake_irq_status & ~mask);
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_get_irq_status(const void *context,
                                      sx126x_irq_mask_t *mask)
{
    (void)context;
    if (fake_fail_get_irq)
        return ESP_FAIL;
    *mask = fake_enforce_payload_limit && fake_rx_length > fake_payload_limit
                ? SX126X_IRQ_NONE
                : fake_irq_status;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_get_and_clear_irq_status(const void *context,
                                                sx126x_irq_mask_t *mask)
{
    (void)context;
    *mask = fake_enforce_payload_limit && fake_rx_length > fake_payload_limit
                ? SX126X_IRQ_NONE
                : fake_irq_status;
    fake_irq_status = SX126X_IRQ_NONE;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_get_rx_buffer_status(const void *context,
                                            sx126x_rx_buffer_status_t *status)
{
    (void)context;
    status->pld_len_in_bytes = fake_rx_length;
    status->buffer_start_pointer = 0u;
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_read_buffer(const void *context, uint8_t offset,
                                   uint8_t *data, uint8_t length)
{
    (void)context;
    (void)offset;
    memcpy(data, fake_rx_data, length);
    return SX126X_STATUS_OK;
}

sx126x_status_t sx126x_get_lora_pkt_status(const void *context,
                                           sx126x_pkt_status_lora_t *status)
{
    (void)context;
    status->rssi_pkt_in_dbm = -70;
    status->snr_pkt_in_db = 7;
    status->signal_rssi_pkt_in_dbm = -72;
    return SX126X_STATUS_OK;
}

uint32_t
sx126x_get_lora_time_on_air_in_ms(const sx126x_pkt_params_lora_t *packet,
                                  const sx126x_mod_params_lora_t *modulation)
{
    (void)packet;
    (void)modulation;
    return fake_airtime_ms;
}

static int init_radio(ninlil_sx1262_radio *radio, bool tx_enabled)
{
    ninlil_rf_profile profile = make_profile(tx_enabled);

    return ninlil_sx1262_radio_init(radio, &profile, true);
}

static int test_init_profile_and_owner(void)
{
    ninlil_sx1262_radio radio;
    uint8_t data = 1u;
    uint8_t output[4];
    uint16_t length = 0u;

    reset_fakes();
    CHECK(init_radio(&radio, true) == NINLIL_OK);
    CHECK(radio.owner_task == &task_a_token);
    CHECK(radio.configured);
    CHECK(radio.rx_active);
    CHECK(fake_reset_calls == 1u);
    CHECK(fake_dio3_order > 0u && fake_dio3_order < fake_dio2_order);
    CHECK(fake_register_writes == 1u);
    CHECK(fake_register_address == SX126X_REG_LR_SYNCWORD);
    CHECK(fake_register_data[0] == UINT8_C(0x14));
    CHECK(fake_register_data[1] == UINT8_C(0x24));
    CHECK(fake_cal_low_mhz == 902u && fake_cal_high_mhz == 928u);
    CHECK(fake_start_rx_calls == 1u);
    CHECK(fake_isr != NULL);

    fake_current_task = &task_b_token;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_INVALID);
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_ERR_INVALID);
    CHECK(ninlil_sx1262_radio_recover(&radio) == NINLIL_ERR_INVALID);
    ninlil_sx1262_radio_deinit(&radio);
    CHECK(radio.configured);
    CHECK(fake_hal_deinit_calls == 0u);

    fake_current_task = &task_a_token;
    ninlil_sx1262_radio_deinit(&radio);
    CHECK(fake_hal_deinit_calls == 1u);
    CHECK(fake_gpio_remove_calls == 1u);
    return 0;
}

static int test_tx_completion_timeout_and_rx_race(void)
{
    ninlil_sx1262_radio radio;
    uint8_t data[4] = {9u, 8u, 7u, 6u};
    uint8_t output[8];
    uint16_t length = 0u;
    unsigned int tx_before;

    reset_fakes();
    CHECK(init_radio(&radio, true) == NINLIL_OK);

    fake_tx_notifies = true;
    fake_tx_completion_irq = SX126X_IRQ_TX_DONE;
    CHECK(ninlil_sx1262_radio_send(&radio, data, sizeof(data)) == NINLIL_OK);
    CHECK(fake_set_tx_calls == 1u);
    CHECK(fake_last_tx_timeout_ms == 600u);
    CHECK(radio.rx_active);

    memcpy(fake_rx_data, "race", 4u);
    fake_rx_length = 4u;
    fake_irq_status = SX126X_IRQ_RX_DONE;
    tx_before = fake_set_tx_calls;
    CHECK(ninlil_sx1262_radio_send(&radio, data, sizeof(data)) ==
          NINLIL_ERR_BUSY);
    CHECK(fake_set_tx_calls == tx_before);
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_OK);
    CHECK(length == 4u && memcmp(output, "race", 4u) == 0);

    fake_tx_notifies = false;
    fake_tx_completion_irq = SX126X_IRQ_NONE;
    CHECK(ninlil_sx1262_radio_send(&radio, data, sizeof(data)) ==
          NINLIL_ERR_TIMEOUT);
    CHECK(radio.timeouts == 1u);
    CHECK(radio.rx_active);
    ninlil_sx1262_radio_deinit(&radio);
    return 0;
}

static int test_receive_polling_irq_and_errors(void)
{
    ninlil_sx1262_radio radio;
    ninlil_sx1262_rx_info info;
    uint8_t output[NINLIL_RADIO_MTU];
    uint16_t length = 0u;

    reset_fakes();
    CHECK(init_radio(&radio, true) == NINLIL_OK);

    /* A short TX must not lower the subsequent explicit-header RX maximum. */
    fake_tx_notifies = true;
    fake_tx_completion_irq = SX126X_IRQ_TX_DONE;
    CHECK(ninlil_sx1262_radio_send(&radio, (const uint8_t *)"tiny", 4u) ==
          NINLIL_OK);
    fake_enforce_payload_limit = true;
    memset(fake_rx_data, 0x53, sizeof(fake_rx_data));
    fake_rx_length = NINLIL_RADIO_MTU;
    fake_irq_status = SX126X_IRQ_RX_DONE;
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_OK);
    CHECK(length == NINLIL_RADIO_MTU &&
          memcmp(output, fake_rx_data, length) == 0);
    fake_enforce_payload_limit = false;

    memcpy(fake_rx_data, "radio", 5u);
    fake_rx_length = 5u;
    fake_irq_status = SX126X_IRQ_RX_DONE;
    CHECK(fake_isr != NULL);
    fake_isr(fake_isr_context);
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      &info, 1u) == NINLIL_OK);
    CHECK(length == 5u && memcmp(output, "radio", 5u) == 0);
    CHECK(info.rssi_dbm == -70 && info.snr_db == 7);

    memcpy(fake_rx_data, "poll", 4u);
    fake_rx_length = 4u;
    fake_irq_status = SX126X_IRQ_RX_DONE;
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_OK);
    CHECK(length == 4u && memcmp(output, "poll", 4u) == 0);

    fake_irq_status = SX126X_IRQ_CRC_ERROR;
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_ERR_INVALID);
    CHECK(radio.crc_errors == 1u && radio.rx_active);

    fake_irq_status = SX126X_IRQ_HEADER_ERROR;
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_ERR_INVALID);
    CHECK(radio.header_errors == 1u && radio.rx_active);

    fake_irq_status = SX126X_IRQ_TIMEOUT;
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_ERR_TIMEOUT);
    CHECK(radio.timeouts == 1u && radio.rx_active);

    fake_rx_length = (uint8_t)(NINLIL_RADIO_MTU + 1u);
    fake_irq_status = SX126X_IRQ_RX_DONE;
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_ERR_TOO_LARGE);
    CHECK(radio.rx_active);
    ninlil_sx1262_radio_deinit(&radio);
    return 0;
}

static int test_recovery_and_fail_closed_profiles(void)
{
    ninlil_sx1262_radio radio;
    ninlil_rf_profile profile;
    uint8_t data = 1u;

    reset_fakes();
    CHECK(init_radio(&radio, true) == NINLIL_OK);
    fake_fail_standby = true;
    CHECK(ninlil_sx1262_radio_recover(&radio) == NINLIL_ERR_IO);
    CHECK(!radio.configured && !radio.rx_active);
    fake_fail_standby = false;
    CHECK(ninlil_sx1262_radio_recover(&radio) == NINLIL_OK);
    CHECK(radio.configured && radio.rx_active);
    ninlil_sx1262_radio_deinit(&radio);

    reset_fakes();
    CHECK(init_radio(&radio, false) == NINLIL_OK);
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_FAULT);
    ninlil_sx1262_radio_deinit(&radio);

    reset_fakes();
    profile = make_profile(true);
    profile.frequency_hz = UINT32_C(900000000);
    CHECK(ninlil_sx1262_radio_init(&radio, &profile, true) ==
          NINLIL_ERR_INVALID);
    CHECK(fake_hal_deinit_calls == 0u);

    reset_fakes();
    profile = make_profile(true);
    profile.frequency_hz = UINT32_C(929000000);
    CHECK(ninlil_sx1262_radio_init(&radio, &profile, true) ==
          NINLIL_ERR_INVALID);
    CHECK(fake_hal_deinit_calls == 0u);

    reset_fakes();
    fake_fail_gpio_config = true;
    CHECK(init_radio(&radio, true) == NINLIL_ERR_IO);
    CHECK(fake_hal_deinit_calls == 1u);

    reset_fakes();
    fake_fail_hal_init = true;
    CHECK(init_radio(&radio, true) == NINLIL_ERR_IO);
    CHECK(fake_hal_deinit_calls == 0u);
    return 0;
}

static int test_jp_channel_and_pause(void)
{
    ninlil_sx1262_radio radio;
    ninlil_rf_profile profile = make_profile(true);
    uint8_t data = 1u;
    int64_t pause_until;

    reset_fakes();
    profile.region = "JP";
    profile.frequency_hz = UINT32_C(921400000);
    CHECK(ninlil_sx1262_radio_init(&radio, &profile, true) == NINLIL_OK);
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_BUSY);
    CHECK(fake_set_tx_calls == 0u && fake_rssi_calls == 0u);
    fake_time_us = radio.tx_not_before_us;
    fake_rssi = -80;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_BUSY);
    CHECK(fake_set_tx_calls == 0u && radio.channel_busy == 1u);
    CHECK(radio.rx_active && fake_bandwidth == SX126X_LORA_BW_125);

    fake_rssi = -81;
    fake_rssi_calls = 0u;
    fake_busy_sample = 26u;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_BUSY);
    CHECK(fake_set_tx_calls == 0u && radio.channel_busy == 2u);
    fake_busy_sample = 0u;
    fake_rssi_calls = 0u;
    fake_rssi = -128;
    fake_tx_notifies = true;
    fake_tx_completion_irq = SX126X_IRQ_TX_DONE;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_OK);
    CHECK(fake_rssi_calls == 26u && fake_set_tx_calls == 1u);
    CHECK(fake_bandwidth == SX126X_LORA_BW_125);
    pause_until = radio.tx_not_before_us;
    CHECK(pause_until - fake_time_us == INT64_C(50000));
    fake_time_us = pause_until - 1;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_BUSY);
    CHECK(fake_set_tx_calls == 1u);
    CHECK(ninlil_sx1262_radio_recover(&radio) == NINLIL_OK);
    CHECK(radio.tx_not_before_us == pause_until);
    fake_time_us = pause_until;
    fake_tx_notifies = false;
    fake_tx_completion_irq = SX126X_IRQ_NONE;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_TIMEOUT);
    CHECK(radio.tx_not_before_us - fake_time_us == INT64_C(650000));
    CHECK(ninlil_sx1262_radio_recover(&radio) == NINLIL_OK);
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_BUSY);
    ninlil_sx1262_radio_deinit(&radio);
    return 0;
}

static int test_jp_fail_closed(void)
{
    ninlil_sx1262_radio radio;
    ninlil_rf_profile profile = make_profile(true);
    uint8_t data = 1u;

    reset_fakes();
    profile.region = "JP";
    profile.frequency_hz = UINT32_C(921400000);
    CHECK(ninlil_sx1262_radio_init(&radio, &profile, true) == NINLIL_OK);
    fake_time_us = radio.tx_not_before_us;
    fake_airtime_ms = 401u;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_TOO_LARGE);
    CHECK(fake_rssi_calls == 0u);
    fake_airtime_ms = 100u;
    fake_bad_chip_status = true;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_IO);
    CHECK(fake_rssi_calls == 0u);
    fake_bad_chip_status = false;
    {
        static const int invalid_statuses[] = {0, 3, 4, 5, 7};
        size_t index;

        for (index = 0u;
             index < sizeof(invalid_statuses) / sizeof(invalid_statuses[0]);
             index++) {
            fake_cmd_status = invalid_statuses[index];
            CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_IO);
            CHECK(fake_rssi_calls == 0u && fake_set_tx_calls == 0u);
        }
        fake_cmd_status = SX126X_CMD_STATUS_RFU;
    }
    fake_rssi = -129;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_IO);
    CHECK(fake_set_tx_calls == 0u);
    fake_rssi = 1;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_IO);
    CHECK(fake_set_tx_calls == 0u);
    fake_rssi = -100;
    fake_rssi_calls = 0u;
    fake_rssi_error_sample = 5u;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_IO);
    CHECK(radio.rx_active && fake_bandwidth == SX126X_LORA_BW_125);
    fake_rssi_error_sample = 0u;
    fake_rssi_calls = 0u;
    fake_clock_stalled = true;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_IO);
    CHECK(fake_rssi_calls == 100u && fake_set_tx_calls == 0u);
    CHECK(radio.rx_active && fake_bandwidth == SX126X_LORA_BW_125);
    ninlil_sx1262_radio_deinit(&radio);

    profile.frequency_hz = UINT32_C(920000000);
    CHECK(ninlil_rf_profile_validate(&profile) == NINLIL_ERR_INVALID);
    profile.frequency_hz = UINT32_C(922400000);
    CHECK(ninlil_rf_profile_validate(&profile) == NINLIL_ERR_INVALID);
    profile.frequency_hz = UINT32_C(921500000);
    CHECK(ninlil_rf_profile_validate(&profile) == NINLIL_ERR_INVALID);
    profile.frequency_hz = UINT32_C(921400000);
    profile.tx_power_dbm = 11;
    CHECK(ninlil_rf_profile_validate(&profile) == NINLIL_ERR_INVALID);
    profile.tx_power_dbm = 10;
    profile.bandwidth_hz = UINT32_C(250000);
    CHECK(ninlil_rf_profile_validate(&profile) == NINLIL_ERR_INVALID);
    profile.bandwidth_hz = UINT32_C(125000);
    CHECK(ninlil_rf_profile_validate(&profile) == NINLIL_OK);
    for (unsigned int stage = 1u; stage <= 5u; stage++) {
        reset_fakes();
        CHECK(ninlil_sx1262_radio_init(&radio, &profile, true) == NINLIL_OK);
        fake_modem_failure = stage;
        fake_time_us = radio.tx_not_before_us;
        CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_IO);
        CHECK(fake_set_tx_calls == 0u);
        if (stage >= 4u)
            CHECK(!radio.configured);
        else
            CHECK(radio.rx_active && fake_packet_type == SX126X_PKT_TYPE_LORA &&
                  fake_sync_restored);
        ninlil_sx1262_radio_deinit(&radio);
    }
    return 0;
}

static int test_receive_in_progress_precedes_transmit(void)
{
    ninlil_sx1262_radio radio;
    uint8_t data = 1u, output[NINLIL_RADIO_MTU];
    uint16_t length = 0u;
    unsigned int starts;
    reset_fakes();
    CHECK(init_radio(&radio, true) == NINLIL_OK);
    fake_tx_notifies = true;
    fake_tx_completion_irq = SX126X_IRQ_TX_DONE;
    fake_irq_status = SX126X_IRQ_PREAMBLE_DETECTED | SX126X_IRQ_HEADER_VALID;
    starts = fake_start_rx_calls;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_BUSY);
    CHECK(fake_set_tx_calls == 0u && fake_start_rx_calls == starts);
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_ERR_EMPTY);
    CHECK(fake_irq_status & SX126X_IRQ_HEADER_VALID);
    fake_time_us += 200000;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_BUSY);
    memset(fake_rx_data, 0x5a, NINLIL_RADIO_MTU);
    fake_rx_length = NINLIL_RADIO_MTU;
    fake_irq_status |= SX126X_IRQ_RX_DONE;
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_OK);
    CHECK(length == NINLIL_RADIO_MTU &&
          memcmp(output, fake_rx_data, length) == 0);
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_OK);
    /* A false preamble cannot hold the transmitter forever. */
    fake_irq_status = SX126X_IRQ_PREAMBLE_DETECTED;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_BUSY);
    fake_time_us += 6000000;
    CHECK(ninlil_sx1262_radio_receive(&radio, output, sizeof(output), &length,
                                      NULL, 0u) == NINLIL_ERR_TIMEOUT);
    CHECK(radio.rx_active && radio.timeouts == 1u);
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_OK);
    ninlil_sx1262_radio_deinit(&radio);
    return 0;
}

static int test_adaptive_power_boundary(void)
{
    ninlil_sx1262_radio radio;
    ninlil_rf_profile p = make_profile(true);
    uint8_t data = 1u;
    reset_fakes();
    fake_tx_notifies = true;
    fake_tx_completion_irq = SX126X_IRQ_TX_DONE;
    p.tx_power_dbm = -3;
    CHECK(ninlil_sx1262_radio_init(&radio, &p, true) == NINLIL_OK);
    CHECK(ninlil_sx1262_radio_power(&radio, -10) == NINLIL_ERR_INVALID);
    CHECK(ninlil_sx1262_radio_power(&radio, 0) == NINLIL_ERR_INVALID);
    CHECK(ninlil_sx1262_radio_power(&radio, -6) == NINLIL_OK);
    CHECK(fake_power == -3 && radio.applied_power_dbm == -3);
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_OK);
    CHECK(fake_power == -6 && radio.applied_power_dbm == -6);
    CHECK(ninlil_sx1262_radio_power(&radio, -9) == NINLIL_OK);
    fake_power_failure = true;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_ERR_IO);
    CHECK(fake_set_tx_calls == 1u && radio.applied_power_dbm == -6);
    fake_power_failure = false;
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) == NINLIL_OK);
    CHECK(fake_power == -9 && radio.applied_power_dbm == -9);
    int64_t pause_until = radio.tx_not_before_us;
    CHECK(ninlil_sx1262_radio_sleep(&radio) == NINLIL_OK);
    CHECK(!radio.configured && !radio.rx_active &&
          radio.tx_not_before_us == pause_until);
    CHECK(ninlil_sx1262_radio_send(&radio, &data, 1u) != NINLIL_OK);
    CHECK(ninlil_sx1262_radio_recover(&radio) == NINLIL_OK);
    CHECK(radio.configured && radio.rx_active &&
          radio.tx_not_before_us == pause_until);
    ninlil_sx1262_radio_deinit(&radio);
    return 0;
}
static int (*const tests[])(void) = {
    test_init_profile_and_owner,
    test_tx_completion_timeout_and_rx_race,
    test_receive_polling_irq_and_errors,
    test_recovery_and_fail_closed_profiles,
    test_jp_channel_and_pause,
    test_jp_fail_closed,
    test_receive_in_progress_precedes_transmit,
    test_adaptive_power_boundary,
};

int main(void)
{
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();

        printf("physical_%02zu %s\n", index + 1u, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
