#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

/**
 * Well-known http connection pool types.
 * NOTE: New connection pools should use the well known name:
 * envoy.filters.connection_pools.http.name
 */
class HttpConnectionPoolNameValues {
public:
  // The normal case: sending HTTP traffic over an HTTP connection.
  const std::string Http = "envoy.filters.connection_pools.http.http";

  // The "CONNECT" upstream: sending decapsulated CONNECT requests over a TCP connection.
  const std::string Tcp = "envoy.filters.connection_pools.http.tcp";

  // The default upstream, returning TCP upstream for CONNECT requests, HTTP for non-CONNECT
  // requests.
  const std::string Default = "envoy.filters.connection_pools.http.default";
};

using HttpConnectionPoolNames = ConstSingleton<HttpConnectionPoolNameValues>;

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
