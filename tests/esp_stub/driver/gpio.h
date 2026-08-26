#ifndef DRIVER_GPIO_H
#define DRIVER_GPIO_H
#include "esp_err.h"
#include <stdint.h>
typedef int gpio_num_t;
typedef struct gpio_config_t {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_POSEDGE 1
#define ESP_INTR_FLAG_IRAM 1
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t gpio, int level);
int gpio_get_level(gpio_num_t gpio);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t gpio, void (*handler)(void *),
                               void *context);
esp_err_t gpio_isr_handler_remove(gpio_num_t gpio);
#endif
