cmake_minimum_required(VERSION 3.20)
project(ninlil_adaptive_components LANGUAGES C)
# Deliberately a component test entry, NOT a replacement for the full SDK CI.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(NINLIL_BUILD_TESTS ON)
option(NINLIL_SANITIZE "ASan/UBSan on the complete component test build" OFF)
if(NINLIL_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
function(ninlil_strict_target target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion
    -Wformat=2 -Wundef -Wcast-align -Wstrict-prototypes -Wmissing-prototypes
    -Wvla -fno-common)
endfunction()
enable_testing()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/adaptive.cmake")
