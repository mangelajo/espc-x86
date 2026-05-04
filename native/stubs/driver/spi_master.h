// ESP-IDF SPI master driver stub
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef void * spi_device_handle_t;
typedef int spi_host_device_t;

#define SPI_MASTER_FREQ_8M   (8000000)
#define SPI_MASTER_FREQ_10M  (10000000)

#define HSPI_HOST  1
#define VSPI_HOST  2

typedef struct {
    uint8_t  command_bits;
    uint8_t  address_bits;
    uint8_t  dummy_bits;
    uint32_t mode;
    int      duty_cycle_pos;
    int      cs_ena_pretrans;
    int      cs_ena_posttrans;
    int      clock_speed_hz;
    int      input_delay_ns;
    int      spics_io_num;
    uint32_t flags;
    int      queue_size;
    void    *pre_cb;
    void    *post_cb;
} spi_device_interface_config_t;

typedef struct {
    int  mosi_io_num;
    int  miso_io_num;
    int  sclk_io_num;
    int  quadwp_io_num;
    int  quadhd_io_num;
    int  max_transfer_sz;
    uint32_t flags;
} spi_bus_config_t;

typedef struct {
    uint32_t flags;
    uint16_t cmd;
    uint64_t addr;
    size_t   length;
    size_t   rxlength;
    void    *user;
    union {
        const void *tx_buffer;
        uint8_t     tx_data[4];
    };
    union {
        void    *rx_buffer;
        uint8_t  rx_data[4];
    };
} spi_transaction_t;

#define SPI_TRANS_USE_TXDATA  (1 << 0)
#define SPI_TRANS_USE_RXDATA  (1 << 1)

inline esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *cfg, int dma_chan) { return ESP_OK; }
inline esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *cfg, spi_device_handle_t *handle) { return ESP_OK; }
inline esp_err_t spi_bus_remove_device(spi_device_handle_t handle) { return ESP_OK; }
inline esp_err_t spi_device_transmit(spi_device_handle_t handle, spi_transaction_t *trans) { return ESP_OK; }
