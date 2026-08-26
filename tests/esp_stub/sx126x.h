#ifndef SX126X_H
#define SX126X_H
#include <stdbool.h>
#include <stdint.h>
typedef int sx126x_status_t;
#define SX126X_STATUS_OK 0
#define SX126X_STANDBY_CFG_RC 0
#define SX126X_REG_MODE_DCDC 1
#define SX126X_TCXO_CTRL_1_8V 2
#define SX126X_CAL_ALL 0x7F
#define SX126X_FALLBACK_STDBY_RC 0x20
#define SX126X_PKT_TYPE_LORA 1
#define SX126X_RAMP_200_US 4
#define SX126X_RX_CONTINUOUS 0x00FFFFFFu
#define SX126X_IRQ_NONE 0u
#define SX126X_IRQ_TX_DONE (1u << 0)
#define SX126X_IRQ_RX_DONE (1u << 1)
#define SX126X_IRQ_HEADER_ERROR (1u << 5)
#define SX126X_IRQ_CRC_ERROR (1u << 6)
#define SX126X_IRQ_TIMEOUT (1u << 9)
#define SX126X_IRQ_ALL 0xFFFFu
typedef uint16_t sx126x_irq_mask_t;
typedef int sx126x_lora_sf_t;
typedef int sx126x_lora_bw_t;
typedef int sx126x_lora_cr_t;
#define SX126X_LORA_BW_125 4
#define SX126X_LORA_BW_250 5
#define SX126X_LORA_BW_500 6
#define SX126X_LORA_CR_4_5 1
#define SX126X_LORA_CR_4_6 2
#define SX126X_LORA_CR_4_7 3
#define SX126X_LORA_CR_4_8 4
#define SX126X_LORA_PKT_EXPLICIT 0
typedef struct sx126x_mod_params_lora_s {
    sx126x_lora_sf_t sf;
    sx126x_lora_bw_t bw;
    sx126x_lora_cr_t cr;
    uint8_t ldro;
} sx126x_mod_params_lora_t;
typedef struct sx126x_pkt_params_lora_s {
    uint16_t preamble_len_in_symb;
    int header_type;
    uint8_t pld_len_in_bytes;
    bool crc_is_on;
    bool invert_iq_is_on;
} sx126x_pkt_params_lora_t;
typedef struct sx126x_pa_cfg_params_s {
    uint8_t pa_duty_cycle;
    uint8_t hp_max;
    uint8_t device_sel;
    uint8_t pa_lut;
} sx126x_pa_cfg_params_t;
typedef struct sx126x_rx_buffer_status_s {
    uint8_t pld_len_in_bytes;
    uint8_t buffer_start_pointer;
} sx126x_rx_buffer_status_t;
typedef struct sx126x_pkt_status_lora_s {
    int8_t rssi_pkt_in_dbm;
    int8_t snr_pkt_in_db;
    int8_t signal_rssi_pkt_in_dbm;
} sx126x_pkt_status_lora_t;
sx126x_status_t sx126x_reset(const void *context);
sx126x_status_t sx126x_set_standby(const void *context, int config);
sx126x_status_t sx126x_set_reg_mode(const void *context, int mode);
sx126x_status_t sx126x_set_dio2_as_rf_sw_ctrl(const void *context, bool enable);
sx126x_status_t sx126x_set_dio3_as_tcxo_ctrl(const void *context, int voltage,
                                              uint32_t timeout);
sx126x_status_t sx126x_cal(const void *context, uint8_t mask);
sx126x_status_t sx126x_cal_img_in_mhz(const void *context, uint16_t low_mhz,
                                      uint16_t high_mhz);
sx126x_status_t sx126x_set_rx_tx_fallback_mode(const void *context, int mode);
sx126x_status_t sx126x_set_pkt_type(const void *context, int type);
sx126x_status_t sx126x_set_rf_freq(const void *context, uint32_t frequency);
sx126x_status_t sx126x_set_lora_mod_params(
    const void *context, const sx126x_mod_params_lora_t *params);
sx126x_status_t sx126x_set_lora_pkt_params(
    const void *context, const sx126x_pkt_params_lora_t *params);
sx126x_status_t sx126x_set_buffer_base_address(const void *context,
                                               uint8_t tx, uint8_t rx);
sx126x_status_t sx126x_set_dio_irq_params(const void *context, uint16_t mask,
                                          uint16_t dio1, uint16_t dio2,
                                          uint16_t dio3);
sx126x_status_t sx126x_set_pa_cfg(const void *context,
                                  const sx126x_pa_cfg_params_t *params);
sx126x_status_t sx126x_set_tx_params(const void *context, int8_t power,
                                     int ramp);
sx126x_status_t sx126x_set_rx_with_timeout_in_rtc_step(const void *context,
                                                       uint32_t timeout);
sx126x_status_t sx126x_set_tx(const void *context, uint32_t timeout_ms);
sx126x_status_t sx126x_write_register(const void *context, uint16_t address,
                                      const uint8_t *data, uint8_t length);
sx126x_status_t sx126x_write_buffer(const void *context, uint8_t offset,
                                    const uint8_t *data, uint8_t length);
sx126x_status_t sx126x_clear_irq_status(const void *context,
                                        sx126x_irq_mask_t mask);
sx126x_status_t sx126x_get_irq_status(const void *context,
                                      sx126x_irq_mask_t *mask);
sx126x_status_t sx126x_get_and_clear_irq_status(const void *context,
                                                sx126x_irq_mask_t *mask);
sx126x_status_t sx126x_get_rx_buffer_status(
    const void *context, sx126x_rx_buffer_status_t *status);
sx126x_status_t sx126x_read_buffer(const void *context, uint8_t offset,
                                   uint8_t *data, uint8_t length);
sx126x_status_t sx126x_get_lora_pkt_status(
    const void *context, sx126x_pkt_status_lora_t *status);
uint32_t sx126x_get_lora_time_on_air_in_ms(
    const sx126x_pkt_params_lora_t *packet,
    const sx126x_mod_params_lora_t *modulation);
#endif
