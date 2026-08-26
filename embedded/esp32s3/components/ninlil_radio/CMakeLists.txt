set(NINLIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "${NINLIL_ROOT}/ports/esp32s3/ninlil_sx1262_hal.c"
        "${NINLIL_ROOT}/ports/esp32s3/ninlil_sx1262_radio.c"
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

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion
    -Wsign-conversion -Wformat=2 -Wundef -Wcast-align -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -fno-common
)
