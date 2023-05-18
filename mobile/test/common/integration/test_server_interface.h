#pragma once

#include "test/common/integration/test_server.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // functions
#endif

/**
 * Starts the server. Can only have one server active per JVM. This is blocking until the port can
 * start accepting requests.
 */
void start_server(bool use_quic, bool disable_https);

/**
 * Shutdowns the server. Can be restarted later. This is blocking until the server has freed all
 * resources.
 */
void shutdown_server();

/**
 * Returns the port that got attributed. Can only be called once the server has been started.
 */
int get_server_port();

/**
 * Set dummy response data for server. Can only be called once the server has been started.
 */
void set_headers_and_data(const std::string& header_key, const std::string& header_value,
                          const std::string& data);

#ifdef __cplusplus
} // functions
#endif
