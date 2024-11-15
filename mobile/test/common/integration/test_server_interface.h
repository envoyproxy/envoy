#pragma once

#include "test/common/integration/test_server.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // functions
#endif

/**
 * Starts the HTTP server. Can only have one HTTP server active per process. This is blocking
 * until the port can start accepting requests.
 */
void start_http_server(Envoy::TestServerType test_server_type);

/**
 * Starts the Proxy server. Can only have one proxy server active per process. This is blocking
 * until the port can start accepting requests.
 */
void start_proxy_server(Envoy::TestServerType test_server_type);

/**
 * Shuts down the HTTP server. Can be restarted later. This is blocking until the server has freed
 * all resources.
 */
void shutdown_http_server();

/**
 * Shuts down the proxy server. Can be restarted later. This is blocking until the server has freed
 * all resources.
 */
void shutdown_proxy_server();

/**
 * Returns the port that got attributed to the HTTP server. Can only be called once the server has
 * been started.
 */
int get_http_server_port();

/**
 * Returns the port that got attributed to the proxy server. Can only be called once the server has
 * been started.
 */
int get_proxy_server_port();

/**
 * Set response data for the HTTP server to return for any URL. Can only be called once the server
 * has been started.
 */
void set_http_headers_and_data(absl::string_view header_key, absl::string_view header_value,
                               absl::string_view response_body);

#ifdef __cplusplus
} // functions
#endif
