idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        ninlil_core
        ninlil_radio
        esp_hw_support
        esp_timer
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
