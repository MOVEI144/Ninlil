cmake_minimum_required(VERSION 3.20)
project(ninlil_m1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NINLIL_SANITIZE "Enable AddressSanitizer and UBSan" OFF)
option(NINLIL_BUILD_SECURE "Build the pinned host-reference secure profile" ON)
option(NINLIL_BUILD_TESTS "Build repository tests and validation tools" ON)

# Instrument external parsers and crypto too, not only the wrapper.
if(NINLIL_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

function(ninlil_strict_target target)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Werror
      -Wshadow
      -Wconversion
      -Wsign-conversion
      -Wformat=2
      -Wundef
      -Wcast-align
      -Wstrict-prototypes
      -Wmissing-prototypes
      -Wvla
      -fno-common
    )
    if(NINLIL_SANITIZE)
      target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
      )
      target_link_options(${target} PRIVATE
        -fsanitize=address,undefined
      )
    endif()
  endif()
endfunction()

add_library(ninlil_contract STATIC src/ninlil_authorization.c)
target_include_directories(ninlil_contract PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
ninlil_strict_target(ninlil_contract)

set(NINLIL_DELIVERY_SOURCES
  src/ninlil_collect.c
  src/ninlil_maintenance.c
  src/ninlil.c
  src/ninlil_custody.c
  src/ninlil_group.c
  src/ninlil_leaf.c
  src/ninlil_policy.c
  src/ninlil_profile.c
  src/ninlil_receive.c
  src/ninlil_replay.c
  src/ninlil_send.c
  src/ninlil_storage.c
  src/ninlil_topology.c
  src/ninlil_wire.c
)

add_library(ninlil_posix STATIC
  ${NINLIL_DELIVERY_SOURCES}
  ports/posix/ninlil_journal.c
)
target_include_directories(ninlil_posix
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
ninlil_strict_target(ninlil_posix)
target_link_libraries(ninlil_posix PUBLIC ninlil_contract)

add_library(ninlil_radio_model STATIC
  src/ninlil_diag.c
  src/ninlil_radio.c
  src/ninlil_rf_profile.c
)
target_include_directories(ninlil_radio_model
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
ninlil_strict_target(ninlil_radio_model)

add_library(ninlil_flash_store STATIC
  ports/flash/ninlil_flash_bank.c
  ports/flash/ninlil_flash_store.c
)
target_include_directories(ninlil_flash_store
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/flash
)
ninlil_strict_target(ninlil_flash_store)

add_library(ninlil_security_state STATIC
  ports/flash/ninlil_security_state.c
)
target_include_directories(ninlil_security_state
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
ninlil_strict_target(ninlil_security_state)

add_library(ninlil_flash_runtime STATIC
  ${NINLIL_DELIVERY_SOURCES}
  ports/flash/ninlil_flash_journal_file.c
)
target_include_directories(ninlil_flash_runtime
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/flash
)
target_link_libraries(ninlil_flash_runtime PUBLIC ninlil_contract PRIVATE ninlil_flash_store)
ninlil_strict_target(ninlil_flash_runtime)

if(NINLIL_BUILD_TESTS)
enable_testing()

add_executable(test_hil_delivery
  tests/test_hil_delivery.c
  tests/test_support.c
  embedded/esp32s3/main/hil_delivery.c
)
target_include_directories(test_hil_delivery PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/embedded/esp32s3/main
)
target_link_libraries(test_hil_delivery PRIVATE ninlil_flash_runtime)
ninlil_strict_target(test_hil_delivery)
add_test(NAME m1_hil_delivery COMMAND test_hil_delivery)

add_executable(m1_flash_inspect tools/m1_flash_inspect.c)
target_link_libraries(m1_flash_inspect PRIVATE ninlil_flash_store)
ninlil_strict_target(m1_flash_inspect)

add_executable(test_core
  tests/test_core.c
  tests/test_support.c
)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_core PRIVATE ninlil_posix)
ninlil_strict_target(test_core)
add_test(NAME m1_core COMMAND test_core)
add_executable(test_quota_retry tests/test_quota_retry.c tests/test_support.c)
target_include_directories(test_quota_retry PRIVATE tests)
target_link_libraries(test_quota_retry PRIVATE ninlil_posix)
ninlil_strict_target(test_quota_retry)
add_test(NAME core_quota_retry COMMAND test_quota_retry)
foreach(backend posix flash_runtime)
  add_executable(test_collection_${backend} tests/test_collection.c tests/test_support.c)
  target_include_directories(test_collection_${backend} PRIVATE tests src)
  target_link_libraries(test_collection_${backend} PRIVATE ninlil_${backend})
  ninlil_strict_target(test_collection_${backend})
  add_test(NAME collection_${backend} COMMAND test_collection_${backend})
endforeach()

add_executable(test_delivery_state
  tests/test_delivery_state.c
  tests/test_support.c
)
target_include_directories(test_delivery_state PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_delivery_state PRIVATE ninlil_posix)
ninlil_strict_target(test_delivery_state)
add_test(NAME p0_delivery_state COMMAND test_delivery_state)
add_executable(test_posix_collection tests/test_posix_collection.c tests/test_support.c)
target_include_directories(test_posix_collection PRIVATE src tests)
target_link_libraries(test_posix_collection PRIVATE ninlil_posix)
target_link_options(test_posix_collection PRIVATE -Wl,--wrap=rename -Wl,--wrap=fsync)
ninlil_strict_target(test_posix_collection)
add_test(NAME posix_collection_publication COMMAND test_posix_collection)

add_executable(test_operations
  tests/test_operations.c
  tests/test_support.c
)
target_include_directories(test_operations PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_operations PRIVATE ninlil_posix)
ninlil_strict_target(test_operations)
add_test(NAME p0_operations COMMAND test_operations)

add_executable(test_diag_radio tests/test_diag_radio.c)
target_link_libraries(test_diag_radio PRIVATE ninlil_radio_model)
ninlil_strict_target(test_diag_radio)
add_test(NAME m1_diag_radio COMMAND test_diag_radio)

add_executable(test_flash tests/test_flash.c)
target_link_libraries(test_flash PRIVATE ninlil_flash_store)
ninlil_strict_target(test_flash)
add_test(NAME m1_flash COMMAND test_flash)
add_executable(test_flash_bank tests/test_flash_bank.c)
target_link_libraries(test_flash_bank PRIVATE ninlil_flash_store)
ninlil_strict_target(test_flash_bank)
add_test(NAME flash_collection_cuts COMMAND test_flash_bank)

add_executable(test_radio_delivery
  tests/test_radio_delivery.c
  tests/test_support.c
)
target_include_directories(test_radio_delivery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_radio_delivery PRIVATE
  ninlil_flash_runtime
  ninlil_radio_model
)
ninlil_strict_target(test_radio_delivery)
add_test(NAME m1_radio_delivery COMMAND test_radio_delivery)

add_executable(test_security_state tests/test_security_state.c)
target_link_libraries(test_security_state PRIVATE ninlil_security_state)
ninlil_strict_target(test_security_state)
add_test(NAME m3_security_state COMMAND test_security_state)

add_executable(test_security_partitions
  tests/test_security_partitions.c
  ports/esp32s3/ninlil_security_partitions.c
)
target_compile_definitions(test_security_partitions PRIVATE ESP_PLATFORM=1)
target_include_directories(test_security_partitions PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/esp_stub
  ${CMAKE_CURRENT_SOURCE_DIR}/ports/esp32s3
)
ninlil_strict_target(test_security_partitions)
add_test(NAME m3_security_partitions COMMAND test_security_partitions)

add_executable(test_sx1262_hal
  tests/test_sx1262_hal.c
  ports/esp32s3/ninlil_sx1262_hal.c
)
target_include_directories(test_sx1262_hal PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/esp_stub
  ${CMAKE_CURRENT_SOURCE_DIR}/ports/esp32s3
)
ninlil_strict_target(test_sx1262_hal)
add_test(NAME m1_sx1262_hal COMMAND test_sx1262_hal)

add_executable(test_sx1262_physical
  tests/test_sx1262_physical.c
  ports/esp32s3/ninlil_sx1262_radio.c
  src/ninlil_rf_profile.c
)
target_include_directories(test_sx1262_physical PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/esp_stub
  ${CMAKE_CURRENT_SOURCE_DIR}/ports/esp32s3
)
ninlil_strict_target(test_sx1262_physical)
add_test(NAME m1_sx1262_physical COMMAND test_sx1262_physical)

add_library(ninlil_sim_support STATIC
  tests/sim/sim_manifest.c
  tests/sim/sim_radio.c
  tests/sim/sim_workload.c
  tests/test_support.c
)
target_include_directories(ninlil_sim_support PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(ninlil_sim_support PUBLIC ninlil_posix)
ninlil_strict_target(ninlil_sim_support)
add_executable(ninlil_sim tests/sim/sim_main.c)
target_link_libraries(ninlil_sim PRIVATE ninlil_sim_support)
ninlil_strict_target(ninlil_sim)

add_executable(test_sim_model tests/sim/test_sim_model.c)
target_include_directories(test_sim_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_sim_model PRIVATE ninlil_sim_support)
ninlil_strict_target(test_sim_model)
add_test(NAME w02_sim_model COMMAND test_sim_model)

add_executable(test_sim_scenarios tests/sim/test_sim_scenarios.c)
target_link_libraries(test_sim_scenarios PRIVATE ninlil_sim_support)
ninlil_strict_target(test_sim_scenarios)
add_test(NAME w02_sim_scenarios COMMAND test_sim_scenarios)
set_tests_properties(w02_sim_scenarios PROPERTIES TIMEOUT 120)
foreach(scenario direct star recovery)
  add_test(NAME w02_${scenario} COMMAND ninlil_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/manifests/${scenario}.txt)
  set_tests_properties(w02_${scenario} PROPERTIES TIMEOUT 60)
endforeach()

endif()
if(NINLIL_BUILD_SECURE)
  include(cmake/security.cmake)
endif()
include(cmake/probe_integration.cmake)
include(cmake/adaptive.cmake)
include(cmake/package.cmake)
