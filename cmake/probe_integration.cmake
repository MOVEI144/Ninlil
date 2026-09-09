# The target is defined in adaptive.cmake before generation. Share one archive
# with component consumers; do not compile duplicate telemetry implementations.
if(TARGET ninlil_control)
  target_link_libraries(ninlil_control PUBLIC ninlil_probe_metrics)
endif()
