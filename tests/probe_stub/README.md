# Probe integration dependency doubles

The component fixture compiles byte-identical copies of the production
`ninlil_node_links.c`, `ninlil_node_radio.c`, `ninlil_node_sleep.c` and
`ninlil_network_pump.c`, and the real pump header. CMake uses COPYONLY, not
function extraction or a rewritten algorithm. Real scheduler, monitor and
link-metrics implementations are linked.

These headers deliberately model only the dependency fields/functions touched
by those translation units. They are **not the real node ABI**, ESP-IDF, EDHOC,
PSA, RF, durable Core, or Coordinator implementation. The test doubles supply
already authenticated inputs, controlled time, a driver outcome and an
observation sink; the sink does not run route selection. No cryptographic,
full-SDK, physical or capacity acceptance is implied.

Tests cover actual code paths for first queue timestamps and coalescing,
BUSY versus TX completion, requested versus applied power, reply finalization,
report encoding/decoding up to the Coordinator callback, delayed observation
ACKs across another probe token, and sleep invalidation. Both explicit workspace
attachment and the Kconfig-preprocessor opt-in path are built. The real SDK
build, ABI and complete network regressions remain required independently.
