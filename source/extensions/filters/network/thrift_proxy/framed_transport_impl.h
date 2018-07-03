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
 * FramedTransportImpl implements the Thrift Framed transport.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-rpc.md
 */
class FramedTransportImpl : public TransportImplBase {
public:
  FramedTransportImpl(TransportCallbacks& callbacks) : TransportImplBase(callbacks) {}

  // Transport
  const std::string& name() const override { return TransportNames::get().FRAMED; }
  bool decodeFrameStart(Buffer::Instance& buffer) override;
  bool decodeFrameEnd(Buffer::Instance& buffer) override;

  static const int32_t MaxFrameSize = 0xFA0000;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
