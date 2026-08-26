#include "ninlil_sx1262_hal.h"
#include "ninlil_board_seeed_b2b.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sx126x_hal.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, \
                    #expression);                                             \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int fake_spi_token;
static int64_t fake_time_us;
static int fake_busy_level;
static esp_err_t fake_gpio_config_result;
static esp_err_t fake_bus_init_result;
static esp_err_t fake_add_device_result;
static esp_err_t fake_acquire_result;
static esp_err_t fake_transmit_result;
static unsigned int fake_gpio_set_calls;
static unsigned int fake_gpio_set_fail_call;
static unsigned int fake_acquire_calls;
static unsigned int fake_release_calls;
static unsigned int fake_transmit_calls;
static unsigned int fake_remove_calls;
static unsigned int fake_free_calls;
static TickType_t fake_last_acquire_wait;
static spi_bus_config_t fake_bus_config;
static spi_device_interface_config_t fake_device_config;
static int fake_levels[64];
static int fake_contract_error;

static void reset_fakes(void)
{
    fake_time_us = 0;
    fake_busy_level = 0;
    fake_gpio_config_result = ESP_OK;
    fake_bus_init_result = ESP_OK;
    fake_add_device_result = ESP_OK;
    fake_acquire_result = ESP_OK;
    fake_transmit_result = ESP_OK;
    fake_gpio_set_calls = 0u;
    fake_gpio_set_fail_call = 0u;
    fake_acquire_calls = 0u;
    fake_release_calls = 0u;
    fake_transmit_calls = 0u;
    fake_remove_calls = 0u;
    fake_free_calls = 0u;
    fake_last_acquire_wait = 0u;
    memset(&fake_bus_config, 0, sizeof(fake_bus_config));
    memset(&fake_device_config, 0, sizeof(fake_device_config));
    memset(fake_levels, 0, sizeof(fake_levels));
    fake_contract_error = 0;
}

esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    return fake_gpio_config_result;
}

esp_err_t gpio_set_level(gpio_num_t gpio, int level)
{
    fake_gpio_set_calls++;
    if (fake_gpio_set_fail_call == fake_gpio_set_calls)
        return ESP_FAIL;
    if (gpio >= 0 && (size_t)gpio < sizeof(fake_levels) / sizeof(fake_levels[0]))
        fake_levels[gpio] = level;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio)
{
    if (gpio == NINLIL_SX1262_PIN_BUSY)
        return fake_busy_level;
    if (gpio >= 0 && (size_t)gpio < sizeof(fake_levels) / sizeof(fake_levels[0]))
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
    (void)gpio;
    (void)handler;
    (void)context;
    return ESP_OK;
}

esp_err_t gpio_isr_handler_remove(gpio_num_t gpio)
{
    (void)gpio;
    return ESP_OK;
}

esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *config, int dma)
{
    if (host != NINLIL_SX1262_SPI_HOST || dma != SPI_DMA_DISABLED) {
        fake_contract_error = 1;
        return ESP_FAIL;
    }
    fake_bus_config = *config;
    return fake_bus_init_result;
}

esp_err_t spi_bus_add_device(spi_host_device_t host,
                             const spi_device_interface_config_t *config,
                             spi_device_handle_t *device)
{
    if (host != NINLIL_SX1262_SPI_HOST) {
        fake_contract_error = 1;
        return ESP_FAIL;
    }
    fake_device_config = *config;
    if (fake_add_device_result == ESP_OK)
        *device = &fake_spi_token;
    return fake_add_device_result;
}

esp_err_t spi_bus_remove_device(spi_device_handle_t device)
{
    if (device != &fake_spi_token) {
        fake_contract_error = 1;
        return ESP_FAIL;
    }
    fake_remove_calls++;
    return ESP_OK;
}

esp_err_t spi_bus_free(spi_host_device_t host)
{
    if (host != NINLIL_SX1262_SPI_HOST) {
        fake_contract_error = 1;
        return ESP_FAIL;
    }
    fake_free_calls++;
    return ESP_OK;
}

esp_err_t spi_device_acquire_bus(spi_device_handle_t device, TickType_t wait)
{
    if (device != &fake_spi_token) {
        fake_contract_error = 1;
        return ESP_FAIL;
    }
    fake_acquire_calls++;
    fake_last_acquire_wait = wait;
    return fake_acquire_result;
}

void spi_device_release_bus(spi_device_handle_t device)
{
    if (device != &fake_spi_token)
        fake_contract_error = 1;
    fake_release_calls++;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t device,
                                      spi_transaction_t *transaction)
{
    if (device != &fake_spi_token || transaction->length % 8u != 0u) {
        fake_contract_error = 1;
        return ESP_FAIL;
    }
    fake_transmit_calls++;
    if (fake_transmit_result == ESP_OK && transaction->rx_buffer) {
        memset(transaction->rx_buffer, UINT8_C(0xA5),
               transaction->length / 8u);
    }
    return fake_transmit_result;
}

int64_t esp_timer_get_time(void)
{
    return fake_time_us;
}

void esp_rom_delay_us(uint32_t us)
{
    fake_time_us += us;
}

static int test_init_io_and_cleanup(void)
{
    ninlil_sx1262_hal_context context;
    uint8_t command[2] = {UINT8_C(0x80), 0u};
    uint8_t write_data[3] = {1u, 2u, 3u};
    uint8_t read_data[3] = {0u, 0u, 0u};

    reset_fakes();
    CHECK(ninlil_sx1262_hal_init(&context) == 0);
    CHECK(context.spi == &fake_spi_token);
    CHECK(context.bus_initialized);
    CHECK(context.nss == NINLIL_SX1262_PIN_NSS);
    CHECK(context.reset == NINLIL_SX1262_PIN_RESET);
    CHECK(context.busy == NINLIL_SX1262_PIN_BUSY);
    CHECK(context.busy_timeout_ms == 20u);
    CHECK(fake_bus_config.mosi_io_num == NINLIL_SX1262_PIN_MOSI);
    CHECK(fake_bus_config.miso_io_num == NINLIL_SX1262_PIN_MISO);
    CHECK(fake_bus_config.sclk_io_num == NINLIL_SX1262_PIN_SCLK);
    CHECK(fake_device_config.clock_speed_hz == NINLIL_SX1262_SPI_HZ);
    CHECK(fake_device_config.mode == 0);
    CHECK(fake_device_config.spics_io_num == -1);
    CHECK(fake_device_config.queue_size == 1);

    CHECK(sx126x_hal_reset(&context) == SX126X_HAL_STATUS_OK);
    CHECK(fake_levels[NINLIL_SX1262_PIN_RESET] == 1);
    CHECK(sx126x_hal_wakeup(&context) == SX126X_HAL_STATUS_OK);
    CHECK(fake_last_acquire_wait == 200u);
    CHECK(fake_levels[NINLIL_SX1262_PIN_NSS] == 1);

    fake_transmit_calls = 0u;
    CHECK(sx126x_hal_write(&context, command, sizeof(command), write_data,
                           sizeof(write_data)) == SX126X_HAL_STATUS_OK);
    CHECK(fake_transmit_calls == 2u);
    CHECK(fake_last_acquire_wait == 20u);

    fake_transmit_calls = 0u;
    CHECK(sx126x_hal_read(&context, command, sizeof(command), read_data,
                          sizeof(read_data)) == SX126X_HAL_STATUS_OK);
    CHECK(fake_transmit_calls == 2u);
    CHECK(read_data[0] == UINT8_C(0xA5));
    CHECK(read_data[1] == UINT8_C(0xA5));
    CHECK(read_data[2] == UINT8_C(0xA5));

    ninlil_sx1262_hal_deinit(&context);
    CHECK(context.spi == NULL);
    CHECK(!context.bus_initialized);
    CHECK(fake_remove_calls == 1u);
    CHECK(fake_free_calls == 1u);
    CHECK(fake_contract_error == 0);
    return 0;
}

static int test_bounded_operation_failures(void)
{
    ninlil_sx1262_hal_context context;
    uint8_t command = UINT8_C(0x80);

    reset_fakes();
    CHECK(ninlil_sx1262_hal_init(&context) == 0);

    fake_busy_level = 1;
    CHECK(sx126x_hal_write(&context, &command, 1u, NULL, 0u) ==
          SX126X_HAL_STATUS_ERROR);
    CHECK(fake_time_us >= INT64_C(20000));
    CHECK(fake_acquire_calls == 0u);

    fake_busy_level = 0;
    fake_acquire_result = ESP_FAIL;
    CHECK(sx126x_hal_write(&context, &command, 1u, NULL, 0u) ==
          SX126X_HAL_STATUS_ERROR);
    CHECK(fake_last_acquire_wait == 20u);

    fake_acquire_result = ESP_OK;
    fake_gpio_set_fail_call = fake_gpio_set_calls + 1u;
    CHECK(sx126x_hal_write(&context, &command, 1u, NULL, 0u) ==
          SX126X_HAL_STATUS_ERROR);
    CHECK(fake_release_calls == 1u);

    fake_gpio_set_fail_call = 0u;
    fake_transmit_result = ESP_FAIL;
    CHECK(sx126x_hal_read(&context, &command, 1u, &command, 1u) ==
          SX126X_HAL_STATUS_ERROR);
    CHECK(fake_levels[NINLIL_SX1262_PIN_NSS] == 1);

    ninlil_sx1262_hal_deinit(&context);
    return 0;
}

static int test_init_and_reset_fail_closed(void)
{
    ninlil_sx1262_hal_context context;

    reset_fakes();
    fake_bus_init_result = ESP_ERR_INVALID_STATE;
    CHECK(ninlil_sx1262_hal_init(&context) != 0);
    CHECK(context.spi == NULL);
    CHECK(!context.bus_initialized);
    CHECK(fake_free_calls == 0u);

    reset_fakes();
    fake_add_device_result = ESP_FAIL;
    CHECK(ninlil_sx1262_hal_init(&context) != 0);
    CHECK(context.spi == NULL);
    CHECK(!context.bus_initialized);
    CHECK(fake_free_calls == 1u);

    reset_fakes();
    CHECK(ninlil_sx1262_hal_init(&context) == 0);
    fake_busy_level = 1;
    CHECK(sx126x_hal_reset(&context) == SX126X_HAL_STATUS_ERROR);
    CHECK(fake_time_us >= INT64_C(206000));
    ninlil_sx1262_hal_deinit(&context);
    CHECK(fake_contract_error == 0);
    return 0;
}

static int (*const tests[])(void) = {
    test_init_io_and_cleanup,
    test_bounded_operation_failures,
    test_init_and_reset_fail_closed,
};

int main(void)
{
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();

        printf("hal_%02zu %s\n", index + 1u, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
