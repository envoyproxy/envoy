#pragma once

#include <atomic>
#include <cstdint>

#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/http/conn_manager_impl.h"

namespace Envoy {
namespace Http {

/**
 * Connection manager utilities split out for ease of testing.
 */
class ConnectionManagerUtility {
public:
  static void mutateRequestHeaders(Http::HeaderMap& request_headers, Protocol protocol,
                                   Network::Connection& connection, ConnectionManagerConfig& config,
                                   const Router::Config& route_config,
                                   Runtime::RandomGenerator& random, Runtime::Loader& runtime,
                                   const LocalInfo::LocalInfo& local_info);

  static void mutateResponseHeaders(Http::HeaderMap& response_headers,
                                    const Http::HeaderMap& request_headers);

private:
  static void mutateXfccRequestHeader(Http::HeaderMap& request_headers,
                                      Network::Connection& connection,
                                      ConnectionManagerConfig& config);
};

} // namespace Http
} // namespace Envoy
