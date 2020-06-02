#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

// FIXME conn pool
/**
 * Well-known http upstream types.
 * NOTE: New upstreams should use the well known name: envoy.filters.upstreams.http.name
 */
class HttpUpstreamsNameValues {
public:
  // The normal case: sending HTTP traffic over an HTTP connection.
  const std::string Http = "envoy.filters.upstreams.http.http";

  // The "CONNECT" upstream: sending decapsulated CONNECT requests over a TCP connection.
  const std::string Tcp = "envoy.filters.upstreams.http.tcp";

  // The default upstream, returning TCP upstream for CONNECT requests, HTTP for non-CONNECT requests.
  const std::string Default = "envoy.filters.upstreams.http.default";
};

using HttpUpstreamsNames = ConstSingleton<HttpUpstreamsNameValues>;

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
