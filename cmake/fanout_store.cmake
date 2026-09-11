# A real journal adapter, separate from the portable state machine.
add_library(ninlil_fanout_store STATIC
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_fanout_store.c"
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_fanout_store_log.c"
  "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_fanout_codec.c")
target_include_directories(ninlil_fanout_store PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include"
  PRIVATE "${NINLIL_ADAPTIVE_ROOT}/src")
target_link_libraries(ninlil_fanout_store PUBLIC ninlil_adaptive)
ninlil_strict_target(ninlil_fanout_store)
add_library(Ninlil::fanout_store ALIAS ninlil_fanout_store)
set_property(GLOBAL APPEND PROPERTY NINLIL_PACKAGE_TARGETS ninlil_fanout_store)

if(NINLIL_BUILD_TESTS AND UNIX)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  add_library(ninlil_fanout_test_journal STATIC
    "${NINLIL_ADAPTIVE_ROOT}/ports/posix/ninlil_journal.c")
  target_include_directories(ninlil_fanout_test_journal PRIVATE
    "${NINLIL_ADAPTIVE_ROOT}/include" "${NINLIL_ADAPTIVE_ROOT}/src")
  ninlil_strict_target(ninlil_fanout_test_journal)
  foreach(name fanout_store fanout_codec)
    add_executable(test_${name} "${NINLIL_ADAPTIVE_ROOT}/tests/test_${name}.c")
    target_include_directories(test_${name} PRIVATE "${NINLIL_ADAPTIVE_ROOT}/src")
    target_link_libraries(test_${name} PRIVATE ninlil_fanout_store
      ninlil_fanout_test_journal OpenSSL::Crypto)
    ninlil_strict_target(test_${name})
    add_test(NAME durable_${name} COMMAND test_${name})
    set_tests_properties(durable_${name} PROPERTIES TIMEOUT 120)
  endforeach()
  target_link_options(test_fanout_store PRIVATE -Wl,--wrap=ninlil_journal_append)
endif()

# Actual Core adapter; the application selects the same journal backend as Core.
add_library(ninlil_fanout_core STATIC "${NINLIL_ADAPTIVE_ROOT}/src/ninlil_fanout_core.c")
target_include_directories(ninlil_fanout_core PUBLIC "${NINLIL_ADAPTIVE_ROOT}/include"
  PRIVATE "${NINLIL_ADAPTIVE_ROOT}/src")
target_link_libraries(ninlil_fanout_core PUBLIC ninlil_fanout_store)
ninlil_strict_target(ninlil_fanout_core)
add_library(Ninlil::fanout_core ALIAS ninlil_fanout_core)
set_property(GLOBAL APPEND PROPERTY NINLIL_PACKAGE_TARGETS ninlil_fanout_core)

if(NINLIL_BUILD_TESTS AND TARGET ninlil_posix)
  foreach(backend posix flash_runtime)
    add_executable(test_delivery_binding_${backend}
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_delivery_binding.c"
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_support.c")
    target_include_directories(test_delivery_binding_${backend} PRIVATE
      "${NINLIL_ADAPTIVE_ROOT}/tests" "${NINLIL_ADAPTIVE_ROOT}/src")
    target_link_libraries(test_delivery_binding_${backend} PRIVATE ninlil_${backend})
    target_link_options(test_delivery_binding_${backend} PRIVATE -Wl,--wrap=ninlil_journal_append)
    ninlil_strict_target(test_delivery_binding_${backend})
    add_test(NAME bound_delivery_${backend} COMMAND test_delivery_binding_${backend})
  endforeach()
endif()

if(NINLIL_BUILD_TESTS AND TARGET ninlil_node)
  foreach(backend posix flash_runtime)
    add_executable(test_node_fanout_bound_${backend}
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_node_fanout_bound.c"
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_support.c"
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_node_bulk.c")
    target_include_directories(test_node_fanout_bound_${backend} PRIVATE
      "${NINLIL_ADAPTIVE_ROOT}/tests" "${NINLIL_ADAPTIVE_ROOT}/src")
    target_link_libraries(test_node_fanout_bound_${backend} PRIVATE ninlil_fanout_core
      ninlil_node ninlil_identity_file ninlil_${backend} ninlil_bulk mbedcrypto)
    ninlil_strict_target(test_node_fanout_bound_${backend})
    add_test(NAME bound_seven_nodes_${backend} COMMAND test_node_fanout_bound_${backend})
    set_tests_properties(bound_seven_nodes_${backend} PROPERTIES TIMEOUT 120)
  endforeach()
endif()

if(NINLIL_BUILD_TESTS AND TARGET ninlil_posix AND UNIX)
  foreach(backend posix flash_runtime)
    add_executable(test_fanout_core_recovery_${backend}
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_fanout_core_recovery.c"
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_support.c")
    target_include_directories(test_fanout_core_recovery_${backend} PRIVATE
      "${NINLIL_ADAPTIVE_ROOT}/tests" "${NINLIL_ADAPTIVE_ROOT}/src")
    target_link_libraries(test_fanout_core_recovery_${backend} PRIVATE
      ninlil_fanout_core ninlil_${backend} OpenSSL::Crypto)
    target_link_options(test_fanout_core_recovery_${backend} PRIVATE
      -Wl,--wrap=ninlil_journal_append -Wl,--wrap=ninlil_submit_bound)
    ninlil_strict_target(test_fanout_core_recovery_${backend})
    add_test(NAME bound_cross_store_${backend} COMMAND test_fanout_core_recovery_${backend})
  endforeach()
endif()

if(NINLIL_BUILD_TESTS AND TARGET ninlil_posix)
  foreach(backend posix flash_runtime)
    add_executable(test_service_spool_${backend}
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_service_spool.c"
      "${NINLIL_ADAPTIVE_ROOT}/tests/test_support.c")
    target_include_directories(test_service_spool_${backend} PRIVATE
      "${NINLIL_ADAPTIVE_ROOT}/src" "${NINLIL_ADAPTIVE_ROOT}/tests")
    target_link_libraries(test_service_spool_${backend} PRIVATE ninlil_${backend})
    ninlil_strict_target(test_service_spool_${backend})
    add_test(NAME durable_service_spool_${backend} COMMAND test_service_spool_${backend})
    set_tests_properties(durable_service_spool_${backend} PROPERTIES TIMEOUT 120)
  endforeach()
endif()
