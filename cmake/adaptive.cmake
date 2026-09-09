# Experimental algorithms with no hidden tasks, crypto or journal backend.
get_filename_component(NINLIL_ADAPTIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(adaptive_sources link_metrics power_policy route_search phy_plan fanout radio_feedback)
set(adaptive_files "")
foreach(module IN LISTS adaptive_sources)
  list(APPEND adaptive_files "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_${module}.c")
endforeach()
add_library(ninlil_adaptive STATIC ${adaptive_files})
target_include_directories(ninlil_adaptive PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include")
ninlil_strict_target(ninlil_adaptive)
add_library(Ninlil::adaptive ALIAS ninlil_adaptive)
# package.cmake exports this independent root too (there are no link children).
set_property(GLOBAL APPEND PROPERTY NINLIL_PACKAGE_TARGETS ninlil_adaptive)

if(NINLIL_BUILD_TESTS)
  foreach(module IN LISTS adaptive_sources)
    add_executable(test_adaptive_${module} "${NINLIL_ADAPTIVE_ROOT}/tests/test_${module}.c")
    target_link_libraries(test_adaptive_${module} PRIVATE ninlil_adaptive)
    ninlil_strict_target(test_adaptive_${module})
    add_test(NAME adaptive_${module} COMMAND test_adaptive_${module})
  endforeach()
  add_executable(test_adaptive_malformed "${NINLIL_ADAPTIVE_ROOT}/tests/test_adaptive_malformed.c")
  target_link_libraries(test_adaptive_malformed PRIVATE ninlil_adaptive)
  ninlil_strict_target(test_adaptive_malformed)
  add_test(NAME adaptive_malformed COMMAND test_adaptive_malformed)
  # Compile exactly the production scheduler, also when crypto is disabled.
  add_library(ninlil_adaptive_test_airtime STATIC "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_airtime.c")
  target_include_directories(ninlil_adaptive_test_airtime PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include")
  ninlil_strict_target(ninlil_adaptive_test_airtime)
  foreach(test airtime airtime_drr)
    add_executable(test_adaptive_${test} "${NINLIL_ADAPTIVE_ROOT}/tests/test_${test}.c")
    target_link_libraries(test_adaptive_${test} PRIVATE ninlil_adaptive_test_airtime)
    ninlil_strict_target(test_adaptive_${test})
    add_test(NAME adaptive_${test} COMMAND test_adaptive_${test})
  endforeach()
  add_executable(test_adaptive_firmware_default
    "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_airtime.c"
    "${NINLIL_ADAPTIVE_ROOT}/tests/test_airtime_default.c")
  target_include_directories(test_adaptive_firmware_default PRIVATE "${NINLIL_ADAPTIVE_ROOT}/include")
  target_compile_definitions(test_adaptive_firmware_default PRIVATE NINLIL_AIRTIME_DEFAULT_DRR=1)
  ninlil_strict_target(test_adaptive_firmware_default)
  add_test(NAME adaptive_firmware_default COMMAND test_adaptive_firmware_default)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME adaptive_seven_hil_protocol
      COMMAND "${Python3_EXECUTABLE}" "${NINLIL_ADAPTIVE_ROOT}/tests/test_seven_hil.py")
  endif()
endif()

# Native fake-hardware integration uses the real pump source and portable
# feedback owner. Keep the ESP-only adapter out of the portable node archive.
if(TARGET test_network_pump)
  target_sources(test_network_pump PRIVATE
    "${NINLIL_ADAPTIVE_ROOT}/ports/esp32s3/ninlil_feedback_pump.c")
  target_link_libraries(test_network_pump PRIVATE ninlil_adaptive)
endif()

# Complete-checkout integration target: real node IO/link/sleep and pump code,
# with named doubles for the remaining Core, crypto and hardware boundaries.
# Vendor headers come from the normal identity target; no sliced ABI headers.
if(NINLIL_BUILD_TESTS AND TARGET ninlil_identity)
  foreach(mode explicit default)
    set(target test_feedback_pump_${mode})
    add_executable(${target}
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_feedback_pump.c"
      "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_node_io.c"
      "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_node_radio.c"
      "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_node_links.c"
      "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_node_sleep.c"
      "${NINLIL_ADAPTIVE_ROOT}/ports/esp32s3/ninlil_network_pump.c"
      "${NINLIL_ADAPTIVE_ROOT}/ports/esp32s3/ninlil_feedback_pump.c"
      "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_airtime.c"
      "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_radio_adapt.c")
    target_include_directories(${target} PRIVATE
      "${NINLIL_ADAPTIVE_ROOT}/src"
      "${NINLIL_ADAPTIVE_ROOT}/tests/esp_stub"
      "${NINLIL_ADAPTIVE_ROOT}/ports/esp32s3")
    target_link_libraries(${target} PRIVATE ninlil_adaptive ninlil_identity)
    if(mode STREQUAL "default")
      target_compile_definitions(${target} PRIVATE NINLIL_FEEDBACK_DEFAULT=1)
    endif()
    ninlil_strict_target(${target})
    add_test(NAME radio_feedback_pump_${mode} COMMAND ${target})
  endforeach()
endif()
