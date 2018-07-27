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
class UnframedTransportImpl : public Transport {
public:
  UnframedTransportImpl() {}

  // Transport
  const std::string& name() const override { return TransportNames::get().UNFRAMED; }
  TransportType type() const override { return TransportType::Unframed; }
  bool decodeFrameStart(Buffer::Instance&, absl::optional<uint32_t>& size) override {
    size.reset();
    return true;
  }
  bool decodeFrameEnd(Buffer::Instance&) override { return true; }
  void encodeFrame(Buffer::Instance& buffer, Buffer::Instance& message) override {
    buffer.move(message);
  }
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
