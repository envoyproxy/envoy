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
  /**
   * Mutates request headers in various ways. This functionality is broken out because of its
   * complexity for ease of testing. See the method itself for detailed comments on what
   * mutations are performed.
   *
   * Note this function may be called twice on the response path if there are
   * 100-Continue headers.
   *
   * @return the final trusted remote address. This depends on various settings and the
   *         existence of the x-forwarded-for header. Again see the method for more details.
   */
  static Network::Address::InstanceConstSharedPtr
  mutateRequestHeaders(Http::HeaderMap& request_headers, Protocol protocol,
                       Network::Connection& connection, ConnectionManagerConfig& config,
                       const Router::Config& route_config, Runtime::RandomGenerator& random,
                       Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info);

  static void mutateResponseHeaders(Http::HeaderMap& response_headers,
                                    const Http::HeaderMap& request_headers, const std::string& via);

private:
  static void mutateXfccRequestHeader(Http::HeaderMap& request_headers,
                                      Network::Connection& connection,
                                      ConnectionManagerConfig& config);
};

} // namespace Http
} // namespace Envoy
