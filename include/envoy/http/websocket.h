#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/network/filter.h"

namespace Envoy {
namespace Http {

/**
 * Callback interface for WebSocket connection management.
 */
class WebSocketProxyCallbacks {
public:
  virtual ~WebSocketProxyCallbacks() {}

  /**
   * Used by a WebSocket implementation to send HTTP error codes back to the
   * client when there are errors establishing a connection to upstream server.
   */
  virtual void sendHeadersOnlyResponse(HeaderMap& headers) PURE;
};

/**
 * An instance of a WebSocketProxy.
 */
class WebSocketProxy {
public:
  virtual ~WebSocketProxy() {}

  /**
   * @see Network::Filter::onData
   */
  virtual Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) PURE;
};
typedef std::unique_ptr<WebSocketProxy> WebSocketProxyPtr;

} // namespace Http
} // namespace Envoy
