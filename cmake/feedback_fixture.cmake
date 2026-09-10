# Both real IO hooks and both observation owners; external dependencies are
# explicit doubles. A PASS here is not a full SDK/ABI or physical RF acceptance.
set(feedback_copy "${CMAKE_CURRENT_BINARY_DIR}/feedback_fixture_sources")
file(MAKE_DIRECTORY "${feedback_copy}")
set(feedback_sources "")
foreach(source src/ninlil_node_io.c src/ninlil_node_radio.c src/ninlil_node_links.c
               src/ninlil_node_sleep.c ports/esp32s3/ninlil_network_pump.c
               ports/esp32s3/ninlil_feedback_pump.c)
  get_filename_component(name "${source}" NAME)
  configure_file("${NINLIL_ADAPTIVE_ROOT}/${source}" "${feedback_copy}/${name}" COPYONLY)
  list(APPEND feedback_sources "${feedback_copy}/${name}")
endforeach()
foreach(header ninlil_network_pump.h ninlil_feedback_pump.h)
  configure_file("${NINLIL_ADAPTIVE_ROOT}/ports/esp32s3/${header}"
                 "${feedback_copy}/${header}" COPYONLY)
endforeach()
foreach(mode explicit default both_default all_default all_adaptive)
  set(target test_feedback_pump_${mode})
  add_executable(${target} ${feedback_sources}
    "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_radio_adapt.c"
    "${NINLIL_ADAPTIVE_ROOT}/tests/test_feedback_pump.c")
  target_include_directories(${target} PRIVATE
    "${feedback_copy}" "${NINLIL_ADAPTIVE_ROOT}/tests/feedback_stub"
    "${NINLIL_ADAPTIVE_ROOT}/include")
  if(NOT mode STREQUAL "explicit")
    target_compile_definitions(${target} PRIVATE CONFIG_NINLIL_RADIO_FEEDBACK_EXPERIMENTAL=1)
  endif()
  if(mode STREQUAL "both_default" OR mode STREQUAL "all_default" OR mode STREQUAL "all_adaptive")
    target_compile_definitions(${target} PRIVATE CONFIG_NINLIL_CLOSED_PROBES_EXPERIMENTAL=1)
  endif()
  if(mode STREQUAL "all_default" OR mode STREQUAL "all_adaptive")
    target_compile_definitions(${target} PRIVATE CONFIG_NINLIL_ROUTE_CANDIDATES_EXPERIMENTAL=1)
  endif()
  target_link_libraries(${target} PRIVATE ninlil_adaptive)
  if(mode STREQUAL "all_adaptive")
    # Compile the production scheduler with its actual firmware definition,
    # exercising DRR + closed probes + confirmed feedback + route candidates.
    target_sources(${target} PRIVATE "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_airtime.c")
    target_compile_definitions(${target} PRIVATE NINLIL_AIRTIME_DEFAULT_DRR=1)
  else()
    target_link_libraries(${target} PRIVATE ninlil_adaptive_test_airtime)
  endif()
  ninlil_strict_target(${target})
  add_test(NAME adaptive_feedback_pump_${mode}_boundary_model COMMAND ${target})
endforeach()
