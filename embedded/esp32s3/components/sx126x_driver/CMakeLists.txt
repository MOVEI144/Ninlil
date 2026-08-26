set(NINLIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(SX126X_ROOT "${NINLIL_ROOT}/third_party/sx126x_driver")

if(NOT EXISTS "${SX126X_ROOT}/src/sx126x.c")
    message(FATAL_ERROR
        "Semtech sx126x_driver is missing. Run scripts/fetch_sx126x_driver.sh")
endif()

idf_component_register(
    SRCS "${SX126X_ROOT}/src/sx126x.c"
    INCLUDE_DIRS "${SX126X_ROOT}/src"
)

# Upstream is pinned and kept byte-for-byte. First-party warning policy is
# applied in the Ninlil components rather than rewriting vendor code.
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11)
