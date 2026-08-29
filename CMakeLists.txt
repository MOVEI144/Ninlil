cmake_minimum_required(VERSION 3.20)
project(ninlil_m1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NINLIL_SANITIZE "Enable AddressSanitizer and UBSan" OFF)

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

set(NINLIL_DELIVERY_SOURCES
  src/ninlil.c
  src/ninlil_authorization.c
  src/ninlil_policy.c
  src/ninlil_profile.c
  src/ninlil_receive.c
  src/ninlil_send.c
  src/ninlil_storage.c
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

add_library(ninlil_radio_model STATIC
  src/ninlil_diag.c
  src/ninlil_radio.c
)
target_include_directories(ninlil_radio_model
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
ninlil_strict_target(ninlil_radio_model)

add_library(ninlil_flash_store STATIC
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
target_link_libraries(ninlil_flash_runtime PRIVATE ninlil_flash_store)
ninlil_strict_target(ninlil_flash_runtime)

enable_testing()

add_executable(test_core
  tests/test_core.c
  tests/test_support.c
)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(test_core PRIVATE ninlil_posix)
ninlil_strict_target(test_core)
add_test(NAME m1_core COMMAND test_core)

add_executable(test_diag_radio tests/test_diag_radio.c)
target_link_libraries(test_diag_radio PRIVATE ninlil_radio_model)
ninlil_strict_target(test_diag_radio)
add_test(NAME m1_diag_radio COMMAND test_diag_radio)

add_executable(test_flash tests/test_flash.c)
target_link_libraries(test_flash PRIVATE ninlil_flash_store)
ninlil_strict_target(test_flash)
add_test(NAME m1_flash COMMAND test_flash)

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
