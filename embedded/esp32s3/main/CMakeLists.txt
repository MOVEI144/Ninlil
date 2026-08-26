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

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion
    -Wsign-conversion -Wformat=2 -Wundef -Wcast-align -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -fno-common
)
