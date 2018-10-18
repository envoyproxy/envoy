#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/network/filter.h"

namespace Envoy {
namespace Http {

/**
 * An instance of a TunnelProxy
 */
class TunnelProxy {
public:
  virtual ~TunnelProxy() {}

  /**
   * @see Network::Filter::onData
   */
  virtual Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) PURE;
};
typedef std::unique_ptr<TunnelProxy> TunnelProxyPtr;

} // namespace Http
} // namespace Envoy
