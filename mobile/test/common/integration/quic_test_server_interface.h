#pragma once

#include "test/common/integration/quic_test_server.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // functions
#endif

/**
 * Starts the server. Can only have one server active per JVM. This is blocking until the port can
 * start accepting requests.
 */
void start_server();

/**
 * Shutdowns the server. Can be restarted later. This is blocking until the server has freed all
 * resources.
 */
void shutdown_server();

/**
 * Returns the port that got attributed. Can only be called once the server has been started.
 */
int get_server_port();

#ifdef __cplusplus
} // functions
#endif
