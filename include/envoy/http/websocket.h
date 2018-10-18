#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/filter.h"

namespace Envoy {
namespace Http {

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
