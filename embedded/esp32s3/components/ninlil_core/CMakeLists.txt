set(NINLIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "${NINLIL_ROOT}/src/ninlil.c" "${NINLIL_ROOT}/src/ninlil_wire.c"
        "${NINLIL_ROOT}/src/ninlil_diag.c" "${NINLIL_ROOT}/src/ninlil_radio.c"
        "${NINLIL_ROOT}/src/ninlil_rf_profile.c"
        "${NINLIL_ROOT}/ports/flash/ninlil_flash_store.c"
        "${NINLIL_ROOT}/ports/flash/ninlil_security_state.c"
        "${NINLIL_ROOT}/ports/esp32s3/ninlil_flash_journal.c"
        "${NINLIL_ROOT}/ports/esp32s3/ninlil_security_partitions.c"
        "${NINLIL_ROOT}/ports/esp32s3/ninlil_flash_admin.c"
    INCLUDE_DIRS
        "${NINLIL_ROOT}/include"
        "${NINLIL_ROOT}/src"
        "${NINLIL_ROOT}/ports/flash"
        "${NINLIL_ROOT}/ports/esp32s3/include"
    REQUIRES esp_partition
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion
    -Wsign-conversion -Wformat=2 -Wundef -Wcast-align -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -fno-common
)
