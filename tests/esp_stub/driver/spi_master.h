#ifndef DRIVER_SPI_MASTER_H
#define DRIVER_SPI_MASTER_H
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
typedef int spi_host_device_t;
typedef void *spi_device_handle_t;
typedef struct spi_bus_config_t {
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;
typedef struct spi_device_interface_config_t {
    int clock_speed_hz;
    int mode;
    int spics_io_num;
    int queue_size;
} spi_device_interface_config_t;
typedef struct spi_transaction_t {
    size_t length;
    const void *tx_buffer;
    void *rx_buffer;
} spi_transaction_t;
#define SPI2_HOST 2
#define SPI_DMA_DISABLED 0
esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *config, int dma);
esp_err_t spi_bus_add_device(spi_host_device_t host,
                             const spi_device_interface_config_t *config,
                             spi_device_handle_t *device);
esp_err_t spi_bus_remove_device(spi_device_handle_t device);
esp_err_t spi_bus_free(spi_host_device_t host);
esp_err_t spi_device_acquire_bus(spi_device_handle_t device,
                                 TickType_t wait);
void spi_device_release_bus(spi_device_handle_t device);
esp_err_t spi_device_polling_transmit(spi_device_handle_t device,
                                      spi_transaction_t *transaction);
#endif
