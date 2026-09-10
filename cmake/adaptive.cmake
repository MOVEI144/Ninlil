# Experimental algorithms with no hidden tasks, crypto or journal backend.
get_filename_component(NINLIL_ADAPTIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(adaptive_sources link_metrics power_policy route_search phy_plan fanout probe_monitor radio_feedback)
set(adaptive_files "")
foreach(module IN LISTS adaptive_sources)
  if(module STREQUAL "link_metrics" OR module STREQUAL "probe_monitor" OR module STREQUAL "route_search")
    continue()
  endif()
  list(APPEND adaptive_files "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_${module}.c")
endforeach()
add_library(ninlil_probe_metrics STATIC
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_link_metrics.c"
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_probe_monitor.c")
target_include_directories(ninlil_probe_metrics PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include")
ninlil_strict_target(ninlil_probe_metrics)
add_library(ninlil_network_planning STATIC
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_network.c"
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_network_route.c"
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_network_wire.c"
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_route_optimizer.c"
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_route_search.c")
target_include_directories(ninlil_network_planning PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include"
  PRIVATE "${NINLIL_ADAPTIVE_ROOT}/src")
ninlil_strict_target(ninlil_network_planning)
# Use one exact archive for the autonomous owner and component consumers.
if(TARGET ninlil_control)
  get_target_property(control_sources ninlil_control SOURCES)
  list(FILTER control_sources EXCLUDE REGEX "src/ninlil_network(_route|_wire)?\\.c$")
  set_property(TARGET ninlil_control PROPERTY SOURCES "${control_sources}")
  target_link_libraries(ninlil_control PUBLIC ninlil_network_planning)
endif()
add_library(ninlil_adaptive STATIC ${adaptive_files})
target_link_libraries(ninlil_adaptive PUBLIC ninlil_probe_metrics ninlil_network_planning)
target_include_directories(ninlil_adaptive PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include")
ninlil_strict_target(ninlil_adaptive)
add_library(Ninlil::adaptive ALIAS ninlil_adaptive)
# The same monitor archive is shared by control and experimental algorithms.
# Register both so core-only installed packages retain the dependency as well.
set_property(GLOBAL APPEND PROPERTY NINLIL_PACKAGE_TARGETS
  ninlil_probe_metrics ninlil_network_planning ninlil_adaptive)

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
  foreach(test airtime airtime_drr airtime_timestamp)
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

if(NINLIL_BUILD_TESTS)
  include("${CMAKE_CURRENT_LIST_DIR}/probe_fixture.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/feedback_fixture.cmake")
endif()

if(TARGET test_network_pump)
  target_sources(test_network_pump PRIVATE
    "${NINLIL_ADAPTIVE_ROOT}/ports/esp32s3/ninlil_feedback_pump.c")
  target_link_libraries(test_network_pump PRIVATE ninlil_adaptive)
endif()

if(NINLIL_BUILD_TESTS)
  add_executable(test_route_optimizer "${NINLIL_ADAPTIVE_ROOT}/tests/test_route_optimizer.c")
  target_link_libraries(test_route_optimizer PRIVATE ninlil_network_planning)
  ninlil_strict_target(test_route_optimizer)
  add_test(NAME adaptive_actual_coordinator_candidates COMMAND test_route_optimizer)
endif()

# Existing regressions, unchanged bytes, available also without the crypto SDK.
if(NINLIL_BUILD_TESTS AND NOT TARGET test_network_restart)
  foreach(name network_restart prepare_lease)
    add_executable(test_existing_${name} "${NINLIL_ADAPTIVE_ROOT}/tests/test_${name}.c")
    target_link_libraries(test_existing_${name} PRIVATE ninlil_network_planning)
    ninlil_strict_target(test_existing_${name})
    add_test(NAME adaptive_existing_${name} COMMAND test_existing_${name})
  endforeach()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/fanout_store.cmake")
