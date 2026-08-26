get_filename_component(NINLIL_RADIO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

idf_component_register(
    SRCS
        "${NINLIL_RADIO_ROOT}/ports/esp32s3/ninlil_sx1262_hal.c"
        "${NINLIL_RADIO_ROOT}/ports/esp32s3/ninlil_sx1262_radio.c"
    INCLUDE_DIRS
        "${NINLIL_RADIO_ROOT}/include"
        "${NINLIL_RADIO_ROOT}/ports/esp32s3"
    REQUIRES
        ninlil_core
        sx126x_driver
        esp_driver_gpio
        esp_driver_spi
        esp_timer
        freertos
)

# ESP-IDF public headers rely on GNU extensions and configuration macros that
# are intentionally undefined for some targets. Compile first-party firmware
# as GNU C11 here; the host syntax gate separately enforces strict ISO C11
# diagnostics against controlled framework stubs.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu11 -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wsign-conversion -Wformat=2 -Wcast-align -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -fno-common
)
