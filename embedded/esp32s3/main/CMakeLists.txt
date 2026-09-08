set(main_sources "app_main.c" "hil_delivery.c")
if(CONFIG_NINLIL_MODE_AUTONOMOUS)
    set(main_sources "node_main.c" "node_storage.c" "node_application.c")
endif()
idf_component_register(
    SRCS ${main_sources}
    INCLUDE_DIRS "."
    REQUIRES
        ninlil_network
        ninlil_core
        ninlil_radio
        bootloader_support
        esp_hw_support
        esp_timer
        esp_driver_gpio
        esp_driver_usb_serial_jtag
        esp_system
        freertos
        log
)
if(NINLIL_NODE_ROSTER_HEADER)
    # The supplied header contains public trust only; private keys stay on the
    # board. An absent roster builds an RF-disabled provisioning console.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        NINLIL_NODE_ROSTER_HEADER="${NINLIL_NODE_ROSTER_HEADER}")
endif()

# ESP-IDF public headers rely on GNU extensions and configuration macros that
# are intentionally undefined for some targets. Compile first-party firmware
# as GNU C11 here; the host syntax gate separately enforces strict ISO C11
# diagnostics against controlled framework stubs.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu11 -Wall -Wextra -Werror -Wshadow -Wconversion
    -Wsign-conversion -Wformat=2 -Wcast-align -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -fno-common
)
