get_filename_component(NINLIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(
  SRCS "${NINLIL_ROOT}/ports/esp32s3/ninlil_network_pump.c"
       "${NINLIL_ROOT}/src/ninlil_secure.c"
       "${NINLIL_ROOT}/src/ninlil_secure_link.c"
       "${NINLIL_ROOT}/ports/crypto/ninlil_psa.c"
       "${NINLIL_ROOT}/ports/crypto/ninlil_edhoc.c"
       "${NINLIL_ROOT}/src/ninlil_join.c"
       "${NINLIL_ROOT}/src/ninlil_join_wire.c"
       "${NINLIL_ROOT}/src/ninlil_network.c"
       "${NINLIL_ROOT}/src/ninlil_network_route.c"
       "${NINLIL_ROOT}/src/ninlil_network_wire.c"
       "${NINLIL_ROOT}/src/ninlil_relay.c"
       "${NINLIL_ROOT}/src/ninlil_routed.c"
       "${NINLIL_ROOT}/src/ninlil_control_log.c"
       "${NINLIL_ROOT}/src/ninlil_control_fragment.c"
       "${NINLIL_ROOT}/src/ninlil_airtime.c"
  INCLUDE_DIRS "${NINLIL_ROOT}/include" "${NINLIL_ROOT}/ports/crypto"
  PRIV_INCLUDE_DIRS "${NINLIL_ROOT}/src"
  REQUIRES ninlil_radio esp_timer ninlil_core ninlil_edhoc_vendor mbedtls
  WHOLE_ARCHIVE
)
target_compile_options(${COMPONENT_LIB} PRIVATE
  -std=gnu11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion
  -Wformat=2 -Wcast-align -Wvla -Wstrict-prototypes -Wmissing-prototypes -fno-common)

target_link_libraries(${COMPONENT_LIB} INTERFACE
  "-Wl,--undefined=ninlil_edhoc_open" "-Wl,--undefined=ninlil_edhoc_exchange"
  "-Wl,--undefined=ninlil_esp_network_step" "-Wl,--undefined=ninlil_routed_receive" "-Wl,--undefined=ninlil_routed_poll"
  "-Wl,--undefined=ninlil_control_log_open" "-Wl,--undefined=ninlil_coordinator_stage" "-Wl,--undefined=ninlil_coordinator_tick")
