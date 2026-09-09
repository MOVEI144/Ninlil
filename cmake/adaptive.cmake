# Experimental algorithms with no hidden tasks, crypto or journal backend.
get_filename_component(NINLIL_ADAPTIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(adaptive_sources link_metrics power_policy route_search phy_plan fanout probe_monitor)
set(adaptive_files "")
foreach(module IN LISTS adaptive_sources)
  if(module STREQUAL "link_metrics" OR module STREQUAL "probe_monitor")
    continue()
  endif()
  list(APPEND adaptive_files "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_${module}.c")
endforeach()
add_library(ninlil_probe_metrics STATIC
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_link_metrics.c"
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_probe_monitor.c")
target_include_directories(ninlil_probe_metrics PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include")
ninlil_strict_target(ninlil_probe_metrics)
add_library(ninlil_adaptive STATIC ${adaptive_files})
target_link_libraries(ninlil_adaptive PUBLIC ninlil_probe_metrics)
target_include_directories(ninlil_adaptive PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include")
ninlil_strict_target(ninlil_adaptive)
add_library(Ninlil::adaptive ALIAS ninlil_adaptive)
# The same monitor archive is shared by control and experimental algorithms.
# Register both so core-only installed packages retain the dependency as well.
set_property(GLOBAL APPEND PROPERTY NINLIL_PACKAGE_TARGETS
  ninlil_probe_metrics ninlil_adaptive)

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
endif()
