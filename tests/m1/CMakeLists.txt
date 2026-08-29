cmake_minimum_required(VERSION 3.20)
project(ninlil_m1_host_tests LANGUAGES C)

get_filename_component(NINLIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NINLIL_SANITIZE "Enable AddressSanitizer and UBSan" OFF)

function(ninlil_m1_strict_target target)
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

set(NINLIL_M1_DELIVERY_SOURCES
  ${NINLIL_ROOT}/src/ninlil.c
  ${NINLIL_ROOT}/src/ninlil_authorization.c
  ${NINLIL_ROOT}/src/ninlil_policy.c
  ${NINLIL_ROOT}/src/ninlil_profile.c
  ${NINLIL_ROOT}/src/ninlil_receive.c
  ${NINLIL_ROOT}/src/ninlil_send.c
  ${NINLIL_ROOT}/src/ninlil_storage.c
  ${NINLIL_ROOT}/src/ninlil_wire.c
)

add_library(ninlil_m1_posix STATIC
  ${NINLIL_M1_DELIVERY_SOURCES}
  ${NINLIL_ROOT}/ports/posix/ninlil_journal.c
)
target_include_directories(ninlil_m1_posix
  PUBLIC ${NINLIL_ROOT}/include
  PRIVATE
    ${NINLIL_ROOT}/src
    ${NINLIL_ROOT}/ports/posix
)
ninlil_m1_strict_target(ninlil_m1_posix)

add_library(ninlil_m1_radio_model STATIC
  ${NINLIL_ROOT}/src/ninlil_diag.c
  ${NINLIL_ROOT}/src/ninlil_radio.c
)
target_include_directories(ninlil_m1_radio_model
  PUBLIC ${NINLIL_ROOT}/include
)
ninlil_m1_strict_target(ninlil_m1_radio_model)

add_library(ninlil_m1_flash_store STATIC
  ${NINLIL_ROOT}/ports/flash/ninlil_flash_store.c
)
target_include_directories(ninlil_m1_flash_store
  PUBLIC
    ${NINLIL_ROOT}/include
    ${NINLIL_ROOT}/ports/flash
)
ninlil_m1_strict_target(ninlil_m1_flash_store)

add_library(ninlil_m1_flash_runtime STATIC
  ${NINLIL_M1_DELIVERY_SOURCES}
  ${NINLIL_ROOT}/ports/flash/ninlil_flash_journal_file.c
)
target_include_directories(ninlil_m1_flash_runtime
  PUBLIC ${NINLIL_ROOT}/include
  PRIVATE
    ${NINLIL_ROOT}/src
    ${NINLIL_ROOT}/ports/flash
)
target_link_libraries(ninlil_m1_flash_runtime PRIVATE ninlil_m1_flash_store)
ninlil_m1_strict_target(ninlil_m1_flash_runtime)

enable_testing()

add_executable(test_m1_core
  ${NINLIL_ROOT}/tests/test_core.c
  ${NINLIL_ROOT}/tests/test_support.c
)
target_include_directories(test_m1_core PRIVATE ${NINLIL_ROOT}/tests)
target_link_libraries(test_m1_core PRIVATE ninlil_m1_posix)
ninlil_m1_strict_target(test_m1_core)
add_test(NAME m1_core COMMAND test_m1_core)

add_executable(test_m1_diag_radio ${NINLIL_ROOT}/tests/test_diag_radio.c)
target_link_libraries(test_m1_diag_radio PRIVATE ninlil_m1_radio_model)
ninlil_m1_strict_target(test_m1_diag_radio)
add_test(NAME m1_diag_radio COMMAND test_m1_diag_radio)

add_executable(test_m1_flash ${NINLIL_ROOT}/tests/test_flash.c)
target_link_libraries(test_m1_flash PRIVATE ninlil_m1_flash_store)
ninlil_m1_strict_target(test_m1_flash)
add_test(NAME m1_flash COMMAND test_m1_flash)

add_executable(test_m1_radio_delivery
  ${NINLIL_ROOT}/tests/test_radio_delivery.c
  ${NINLIL_ROOT}/tests/test_support.c
)
target_include_directories(test_m1_radio_delivery PRIVATE ${NINLIL_ROOT}/tests)
target_link_libraries(test_m1_radio_delivery PRIVATE
  ninlil_m1_flash_runtime
  ninlil_m1_radio_model
)
ninlil_m1_strict_target(test_m1_radio_delivery)
add_test(NAME m1_radio_delivery COMMAND test_m1_radio_delivery)

add_executable(test_m1_sx1262_hal
  ${NINLIL_ROOT}/tests/test_sx1262_hal.c
  ${NINLIL_ROOT}/ports/esp32s3/ninlil_sx1262_hal.c
)
target_include_directories(test_m1_sx1262_hal PRIVATE
  ${NINLIL_ROOT}/tests/esp_stub
  ${NINLIL_ROOT}/ports/esp32s3
)
ninlil_m1_strict_target(test_m1_sx1262_hal)
add_test(NAME m1_sx1262_hal COMMAND test_m1_sx1262_hal)

add_executable(test_m1_sx1262_physical
  ${NINLIL_ROOT}/tests/test_sx1262_physical.c
  ${NINLIL_ROOT}/ports/esp32s3/ninlil_sx1262_radio.c
  ${NINLIL_ROOT}/src/ninlil_rf_profile.c
)
target_include_directories(test_m1_sx1262_physical PRIVATE
  ${NINLIL_ROOT}/include
  ${NINLIL_ROOT}/tests/esp_stub
  ${NINLIL_ROOT}/ports/esp32s3
)
ninlil_m1_strict_target(test_m1_sx1262_physical)
add_test(NAME m1_sx1262_physical COMMAND test_m1_sx1262_physical)
