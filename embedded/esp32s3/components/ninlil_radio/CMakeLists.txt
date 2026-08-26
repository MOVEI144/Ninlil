set(NINLIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "${NINLIL_ROOT}/ports/esp32s3/ninlil_sx1262_hal.c"
        "${NINL_ROOT}/ports/esp32s3/ninlil_sx1262_radio.c"
    INCLUDE_DIRS
        "${NINLIL_ROOT}/include"
        "${NINLIL_ROOT}/ports/esp32s3"
    REQUIRES
        sx126x_driver
        esp_driver_gpio
        esp_driver_spi
        esp_timer
        freertos
)

# ESP-IDF public headers intentionally use GCC extensions such as
# `#include_next` and test configuration macros that may be undefined. Keep
# first-party warnings strict here, while the host ESP syntax gate separately
# enforces -Wpedantic and -Wundef against stubbed framework headers.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wsign-conversion -Wformat=2 -Wcast-align -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -fno-common
)
