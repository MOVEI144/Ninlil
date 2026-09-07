set(main_sources "app_main.c" "hil_delivery.c")
if(CONFIG_NINLIL_M1_MODE_SECURE_BENCH)
    list(APPEND main_sources "secure_bench.c" "secure_bench_crypto.c" "bench_sessions.c" "bench_relay.c")
endif()
idf_component_register(
    SRCS ${main_sources}
    INCLUDE_DIRS "."
    REQUIRES
        ninlil_network
        ninlil_core
        ninlil_radio
        esp_hw_support
        esp_timer
        esp_driver_gpio
        esp_driver_usb_serial_jtag
        esp_system
        freertos
        log
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
