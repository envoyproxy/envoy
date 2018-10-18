#pragma once

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Callback interface for WebSocket/TunnelProxy connection management.
 */
class HeadersOnlyCallback {
public:
  virtual ~HeadersOnlyCallback() {}

  /**
   * Used by a WebSocket implementation to send HTTP error codes back to the
   * client when there are errors establishing a connection to upstream server.
   */
  virtual void sendHeadersOnlyResponse(HeaderMap& headers, bool end_of_stream = true) PURE;
};

} // namespace Http
} // namespace Envoy
