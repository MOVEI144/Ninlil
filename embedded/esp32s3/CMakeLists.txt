cmake_minimum_required(VERSION 3.20)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ninlil_m1)
if(NINLIL_SLEEP_PROBE)
    target_sources(ninlil_m1.elf PRIVATE ../../tools/node_hil/sleep_probe.c)
    target_link_libraries(ninlil_m1.elf PRIVATE idf::nvs_flash idf::ninlil_radio)
    target_compile_options(ninlil_m1.elf PRIVATE -Wall -Wextra -Werror -Wshadow
        -Wconversion -Wsign-conversion -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes)
    target_link_options(ninlil_m1.elf PRIVATE
        -Wl,--wrap=esp_light_sleep_start
        -Wl,--wrap=ninlil_sx1262_radio_recover
        -Wl,--wrap=bootloader_random_enable)
endif()
