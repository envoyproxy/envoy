#pragma once

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Callback interface for WebSocket connection management.
 */
class WsHandlerCallbacks {
public:
  virtual ~WsHandlerCallbacks() {}

  /**
   * Used by a WebSocket implementation to send HTTP error codes back to the
   * client when there are errors establishing a connection to upstream server.
   */
  virtual void sendHeadersOnlyResponse(HeaderMap& headers) PURE;
};

} // namespace Http
} // namespace Envoy
