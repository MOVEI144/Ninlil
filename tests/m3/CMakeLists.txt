cmake_minimum_required(VERSION 3.20)
project(ninlil_m3_security_tests LANGUAGES C)

get_filename_component(NINLIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NINLIL_SANITIZE "Enable AddressSanitizer and UBSan" OFF)

function(ninlil_m3_strict_target target)
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

add_library(ninlil_m3_security_state STATIC
  ${NINLIL_ROOT}/ports/flash/ninlil_security_state.c
)
target_include_directories(ninlil_m3_security_state
  PUBLIC ${NINLIL_ROOT}/include
)
ninlil_m3_strict_target(ninlil_m3_security_state)

add_executable(test_m3_security_state
  ${NINLIL_ROOT}/tests/test_security_state.c
)
target_link_libraries(test_m3_security_state PRIVATE ninlil_m3_security_state)
ninlil_m3_strict_target(test_m3_security_state)

add_executable(test_m3_security_partitions
  ${NINLIL_ROOT}/tests/test_security_partitions.c
  ${NINLIL_ROOT}/ports/esp32s3/ninlil_security_partitions.c
)
target_compile_definitions(test_m3_security_partitions PRIVATE ESP_PLATFORM=1)
target_include_directories(test_m3_security_partitions PRIVATE
  ${NINLIL_ROOT}/include
  ${NINLIL_ROOT}/tests/esp_stub
  ${NINLIL_ROOT}/ports/esp32s3
)
ninlil_m3_strict_target(test_m3_security_partitions)

enable_testing()
add_test(NAME m3_security_state COMMAND test_m3_security_state)
add_test(NAME m3_security_partitions COMMAND test_m3_security_partitions)
