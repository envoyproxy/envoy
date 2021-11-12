#pragma once

#include "envoy/upstream/upstream.h"

#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Extends Upstream::ProtocolOptionsConfig with Thrift-specific cluster options.
 */
class ProtocolOptionsConfig : public Upstream::ProtocolOptionsConfig {
public:
  ~ProtocolOptionsConfig() override = default;

  virtual TransportType transport(TransportType downstream_transport) const PURE;
  virtual ProtocolType protocol(ProtocolType downstream_protocol) const PURE;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
