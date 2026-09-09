# Typed boundary test, NOT a replacement for the full node/SDK integration gate.
# Copy the exact implementation bytes so quoted includes select explicit doubles
# instead of requiring a downloaded EDHOC/PSA/ESP-IDF tree in component-only CI.
set(probe_copy "${CMAKE_CURRENT_BINARY_DIR}/probe_fixture_sources")
file(MAKE_DIRECTORY "${probe_copy}")
set(probe_sources "")
foreach(source src/ninlil_node_radio.c src/ninlil_node_links.c
               src/ninlil_node_sleep.c ports/esp32s3/ninlil_network_pump.c)
  get_filename_component(name "${source}" NAME)
  configure_file("${NINLIL_ADAPTIVE_ROOT}/${source}" "${probe_copy}/${name}" COPYONLY)
  list(APPEND probe_sources "${probe_copy}/${name}")
endforeach()
configure_file("${NINLIL_ADAPTIVE_ROOT}/ports/esp32s3/ninlil_network_pump.h"
               "${probe_copy}/ninlil_network_pump.h" COPYONLY)
add_executable(test_probe_pipeline ${probe_sources}
    "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_radio_adapt.c"
    "${NINLIL_ADAPTIVE_ROOT}/tests/test_probe_pipeline.c")
target_include_directories(test_probe_pipeline PRIVATE "${probe_copy}"
    "${NINLIL_ADAPTIVE_ROOT}/tests/probe_stub"
    "${NINLIL_ADAPTIVE_ROOT}/include")
target_link_libraries(test_probe_pipeline PRIVATE ninlil_adaptive ninlil_adaptive_test_airtime)
ninlil_strict_target(test_probe_pipeline)
add_test(NAME adaptive_probe_pipeline_boundary_model COMMAND test_probe_pipeline)

add_executable(test_probe_pipeline_default ${probe_sources}
    "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_radio_adapt.c"
    "${NINLIL_ADAPTIVE_ROOT}/tests/test_probe_pipeline.c")
target_include_directories(test_probe_pipeline_default PRIVATE "${probe_copy}"
    "${NINLIL_ADAPTIVE_ROOT}/tests/probe_stub"
    "${NINLIL_ADAPTIVE_ROOT}/include")
target_compile_definitions(test_probe_pipeline_default PRIVATE
    CONFIG_NINLIL_CLOSED_PROBES_EXPERIMENTAL=1)
target_link_libraries(test_probe_pipeline_default PRIVATE
    ninlil_adaptive ninlil_adaptive_test_airtime)
ninlil_strict_target(test_probe_pipeline_default)
add_test(NAME adaptive_probe_pipeline_opt_in_model COMMAND test_probe_pipeline_default)
