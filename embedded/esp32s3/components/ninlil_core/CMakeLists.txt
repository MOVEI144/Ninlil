get_filename_component(NINLIL_CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

idf_component_register(
    SRCS
        "${NINLIL_CORE_ROOT}/src/ninlil.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_authorization.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_policy.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_profile.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_receive.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_send.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_storage.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_wire.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_diag.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_radio.c"
        "${NINLIL_CORE_ROOT}/src/ninlil_rf_profile.c"
        "${NINLIL_CORE_ROOT}/ports/flash/ninlil_flash_store.c"
        "${NINLIL_CORE_ROOT}/ports/flash/ninlil_security_state.c"
        "${NINLIL_CORE_ROOT}/ports/esp32s3/ninlil_flash_journal.c"
        "${NINLIL_CORE_ROOT}/ports/esp32s3/ninlil_security_partitions.c"
        "${NINLIL_CORE_ROOT}/ports/esp32s3/ninlil_flash_admin.c"
    INCLUDE_DIRS
        "${NINLIL_CORE_ROOT}/include"
        "${NINLIL_CORE_ROOT}/src"
        "${NINLIL_CORE_ROOT}/ports/flash"
        "${NINLIL_CORE_ROOT}/ports/esp32s3/include"
    REQUIRES esp_partition
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
