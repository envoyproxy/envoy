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
void start_server(Envoy::TestServerType test_server_type);

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
 * Set response data for server to return for any URL. Can only be called once the server has been
 * started.
 */
void set_headers_and_data(absl::string_view header_key, absl::string_view header_value,
                          absl::string_view response_body);

#ifdef __cplusplus
} // functions
#endif
