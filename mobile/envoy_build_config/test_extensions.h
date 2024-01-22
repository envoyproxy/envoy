#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // functions
#endif

void register_test_extensions();

// Registers extensions required for the Envoy server listener module (e.g. when running Envoy as a
// proxy server).
void register_test_extensions_for_server();

#ifdef __cplusplus
} // functions
#endif
