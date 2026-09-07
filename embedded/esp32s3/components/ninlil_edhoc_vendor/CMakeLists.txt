get_filename_component(NINLIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(EDHOC_ROOT "${NINLIL_ROOT}/third_party/libedhoc")
include("${EDHOC_ROOT}/cmake/sources.cmake")
include("${EDHOC_ROOT}/cmake/externals_sources.cmake")
list(FILTER LIBEDHOC_ZCBOR_SOURCES EXCLUDE REGEX "/zcbor_encode\\.c$")
list(APPEND LIBEDHOC_ZCBOR_SOURCES "${NINLIL_ROOT}/third_party/adapted/zcbor_encode.c")
set(cbor_adapted_sources "")
foreach(cbor_source IN LISTS LIBEDHOC_BACKEND_CBOR_SOURCES)
  get_filename_component(cbor_name "${cbor_source}" NAME)
  list(APPEND cbor_adapted_sources "${NINLIL_ROOT}/third_party/adapted/cbor/${cbor_name}")
endforeach()
set(LIBEDHOC_BACKEND_CBOR_SOURCES "${cbor_adapted_sources}")
set(CONFIG_LIBEDHOC_ENABLE 1)
set(CONFIG_LIBEDHOC_KEY_ID_LEN 4)
set(CONFIG_LIBEDHOC_MAX_NR_OF_CIPHER_SUITES 3)
set(CONFIG_LIBEDHOC_MAX_LEN_OF_CONN_ID 7)
set(CONFIG_LIBEDHOC_MAX_LEN_OF_ECC_KEY 48)
set(CONFIG_LIBEDHOC_MAX_LEN_OF_MAC 48)
set(CONFIG_LIBEDHOC_MAX_NR_OF_EAD_TOKENS 3)
set(CONFIG_LIBEDHOC_MAX_LEN_OF_CRED_KEY_ID 1)
set(CONFIG_LIBEDHOC_MAX_LEN_OF_HASH_ALG 1)
set(CONFIG_LIBEDHOC_MAX_NR_OF_CERTS_IN_X509_CHAIN 2)
set(CONFIG_LIBEDHOC_LOG_LEVEL 0)
set(CONFIG_LIBEDHOC_MEM_BACKEND 2)
configure_file("${EDHOC_ROOT}/cmake/edhoc_config.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/edhoc_config.h" @ONLY)
idf_component_register(
  SRCS ${LIBEDHOC_CORE_SOURCES} ${LIBEDHOC_BACKEND_CBOR_SOURCES}
       ${LIBEDHOC_ZCBOR_SOURCES} "${NINLIL_ROOT}/third_party/adapted/edhoc_cipher_suite_2.c"
  INCLUDE_DIRS "${EDHOC_ROOT}/include" "${EDHOC_ROOT}/helpers/include"
       "${CMAKE_CURRENT_BINARY_DIR}/generated"
  PRIV_INCLUDE_DIRS ${LIBEDHOC_BACKEND_CBOR_INCLUDE_DIR}
       ${LIBEDHOC_BACKEND_MEM_INCLUDE_DIR} ${LIBEDHOC_BACKEND_LOG_INCLUDE_DIR}
       ${LIBEDHOC_ZCBOR_INCLUDE_DIR}
  REQUIRES mbedtls
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ZCBOR_CANONICAL)
