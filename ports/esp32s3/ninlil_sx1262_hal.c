#include "ninlil_sx1262_hal.h"
#include "ninlil_board_seeed_b2b.h"

#include "freertos/FreeRTOS.h"

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sx126x_hal.h"

#include <string.h>

#define SX126X_GET_STATUS_OPCODE 0xC0u
#define DEFAULT_BUSY_TIMEOUT_MS 20u
#define RESET_BUSY_TIMEOUT_MS 200u

static sx126x_hal_status_t hal_error(void)
{
    return SX126X_HAL_STATUS_ERROR;
}

static int set_level(gpio_num_t gpio, int level)
{
    return gpio_set_level(gpio, level) == ESP_OK ? 0 : -1;
}

static int wait_busy(const ninlil_sx1262_hal_context *context,
                     uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (gpio_get_level(context->busy) != 0) {
        if (esp_timer_get_time() >= deadline)
            return -1;
        esp_rom_delay_us(20u);
    }
    return 0;
}

static esp_err_t transmit(spi_device_handle_t spi, const void *tx,
                          void *rx, size_t length)
{
    spi_transaction_t transaction;

    if (length == 0u)
        return ESP_OK;
    memset(&transaction, 0, sizeof(transaction));
    transaction.length = length * 8u;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;
    return spi_device_polling_transmit(spi, &transaction);
}

static int select_radio(const ninlil_sx1262_hal_context *context)
{
    return set_level(context->nss, 0);
}

static int deselect_radio(const ninlil_sx1262_hal_context *context)
{
    return set_level(context->nss, 1);
}

int ninlil_sx1262_hal_init(ninlil_sx1262_hal_context *context)
{
    spi_bus_config_t bus;
    spi_device_interface_config_t device;
    gpio_config_t gpio;
    esp_err_t rc;

    if (!context)
        return -1;
    memset(context, 0, sizeof(*context));
    context->nss = (gpio_num_t)NINLIL_SX1262_PIN_NSS;
    context->reset = (gpio_num_t)NINLIL_SX1262_PIN_RESET;
    context->busy = (gpio_num_t)NINLIL_SX1262_PIN_BUSY;
    context->busy_timeout_ms = DEFAULT_BUSY_TIMEOUT_MS;

    memset(&gpio, 0, sizeof(gpio));
    gpio.pin_bit_mask = (UINT64_C(1) << context->nss) |
                        (UINT64_C(1) << context->reset);
    gpio.mode = GPIO_MODE_OUTPUT;
    gpio.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&gpio) != ESP_OK || deselect_radio(context) != 0 ||
        set_level(context->reset, 1) != 0)
        return -1;

    memset(&gpio, 0, sizeof(gpio));
    gpio.pin_bit_mask = UINT64_C(1) << context->busy;
    gpio.mode = GPIO_MODE_INPUT;
    gpio.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&gpio) != ESP_OK)
        return -1;

    memset(&bus, 0, sizeof(bus));
    bus.mosi_io_num = NINLIL_SX1262_PIN_MOSI;
    bus.miso_io_num = NINLIL_SX1262_PIN_MISO;
    bus.sclk_io_num = NINLIL_SX1262_PIN_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 320;
    rc = spi_bus_initialize(NINLIL_SX1262_SPI_HOST, &bus, SPI_DMA_DISABLED);
    if (rc != ESP_OK)
        return -1;
    context->bus_initialized = true;

    memset(&device, 0, sizeof(device));
    device.clock_speed_hz = NINLIL_SX1262_SPI_HZ;
    device.mode = 0;
    device.spics_io_num = -1;
    device.queue_size = 1;
    rc = spi_bus_add_device(NINLIL_SX1262_SPI_HOST, &device, &context->spi);
    if (rc != ESP_OK) {
        (void)spi_bus_free(NINLIL_SX1262_SPI_HOST);
        context->bus_initialized = false;
        return -1;
    }
    return 0;
}

void ninlil_sx1262_hal_deinit(ninlil_sx1262_hal_context *context)
{
    if (!context)
        return;
    (void)deselect_radio(context);
    (void)set_level(context->reset, 1);
    if (context->spi) {
        (void)spi_bus_remove_device(context->spi);
        context->spi = NULL;
    }
    if (context->bus_initialized) {
        (void)spi_bus_free(NINLIL_SX1262_SPI_HOST);
        context->bus_initialized = false;
    }
}

sx126x_hal_status_t sx126x_hal_reset(const void *opaque)
{
    const ninlil_sx1262_hal_context *context = opaque;

    if (!context || set_level(context->reset, 0) != 0)
        return hal_error();
    esp_rom_delay_us(1000u);
    if (set_level(context->reset, 1) != 0)
        return hal_error();
    esp_rom_delay_us(5000u);
    return wait_busy(context, RESET_BUSY_TIMEOUT_MS) == 0
               ? SX126X_HAL_STATUS_OK
               : hal_error();
}

sx126x_hal_status_t sx126x_hal_wakeup(const void *opaque)
{
    const ninlil_sx1262_hal_context *context = opaque;
    uint8_t command[2] = {SX126X_GET_STATUS_OPCODE, 0u};
    esp_err_t rc;
    int deselect_result;

    if (!context || !context->spi)
        return hal_error();
    if (spi_device_acquire_bus(context->spi,
                               pdMS_TO_TICKS(RESET_BUSY_TIMEOUT_MS)) != ESP_OK)
        return hal_error();
    if (select_radio(context) != 0) {
        spi_device_release_bus(context->spi);
        return hal_error();
    }
    rc = transmit(context->spi, command, NULL, sizeof(command));
    deselect_result = deselect_radio(context);
    spi_device_release_bus(context->spi);
    if (rc != ESP_OK || deselect_result != 0)
        return hal_error();
    return wait_busy(context, RESET_BUSY_TIMEOUT_MS) == 0
               ? SX126X_HAL_STATUS_OK
               : hal_error();
}

sx126x_hal_status_t sx126x_hal_write(const void *opaque,
                                     const uint8_t *command,
                                     uint16_t command_length,
                                     const uint8_t *data,
                                     uint16_t data_length)
{
    const ninlil_sx1262_hal_context *context = opaque;
    esp_err_t rc;
    int deselect_result;

    if (!context || !context->spi || !command || command_length == 0u ||
        (data_length > 0u && !data) ||
        wait_busy(context, context->busy_timeout_ms) != 0)
        return hal_error();
    if (spi_device_acquire_bus(context->spi,
                               pdMS_TO_TICKS(context->busy_timeout_ms)) !=
        ESP_OK)
        return hal_error();
    if (select_radio(context) != 0) {
        spi_device_release_bus(context->spi);
        return hal_error();
    }
    rc = transmit(context->spi, command, NULL, command_length);
    if (rc == ESP_OK)
        rc = transmit(context->spi, data, NULL, data_length);
    deselect_result = deselect_radio(context);
    spi_device_release_bus(context->spi);
    if (rc != ESP_OK || deselect_result != 0 ||
        wait_busy(context, context->busy_timeout_ms) != 0)
        return hal_error();
    return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_read(const void *opaque,
                                    const uint8_t *command,
                                    uint16_t command_length, uint8_t *data,
                                    uint16_t data_length)
{
    const ninlil_sx1262_hal_context *context = opaque;
    esp_err_t rc;
    int deselect_result;

    if (!context || !context->spi || !command || command_length == 0u ||
        (data_length > 0u && !data) ||
        wait_busy(context, context->busy_timeout_ms) != 0)
        return hal_error();
    if (spi_device_acquire_bus(context->spi,
                               pdMS_TO_TICKS(context->busy_timeout_ms)) !=
        ESP_OK)
        return hal_error();
    if (select_radio(context) != 0) {
        spi_device_release_bus(context->spi);
        return hal_error();
    }
    rc = transmit(context->spi, command, NULL, command_length);
    if (rc == ESP_OK)
        rc = transmit(context->spi, NULL, data, data_length);
    deselect_result = deselect_radio(context);
    spi_device_release_bus(context->spi);
    if (rc != ESP_OK || deselect_result != 0 ||
        wait_busy(context, context->busy_timeout_ms) != 0)
        return hal_error();
    return SX126X_HAL_STATUS_OK;
}
