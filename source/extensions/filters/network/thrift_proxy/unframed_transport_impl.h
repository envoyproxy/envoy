#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "extensions/filters/network/thrift_proxy/transport_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * UnframedTransportImpl implements the Thrift Unframed transport.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-rpc.md
 */
class UnframedTransportImpl : public TransportImplBase {
public:
  UnframedTransportImpl(TransportCallbacks& callbacks) : TransportImplBase(callbacks) {}

  // Transport
  const std::string& name() const override { return TransportNames::get().UNFRAMED; }
  bool decodeFrameStart(Buffer::Instance&) override {
    onFrameStart(absl::optional<uint32_t>());
    return true;
  }
  bool decodeFrameEnd(Buffer::Instance&) override {
    onFrameComplete();
    return true;
  }
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
