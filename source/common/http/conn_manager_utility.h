#pragma once

#include "conn_manager_impl.h"

#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

namespace Http {

/**
 * Connection manager utilities split out for ease of testing.
 */
class ConnectionManagerUtility {
public:
  static void mutateRequestHeaders(Http::HeaderMap& request_headers,
                                   Network::Connection& connection, ConnectionManagerConfig& config,
                                   Runtime::RandomGenerator& random, Runtime::Loader& runtime);

  static void mutateResponseHeaders(Http::HeaderMap& response_headers,
                                    const Http::HeaderMap& request_headers,
                                    ConnectionManagerConfig& config);
};

} // Http
