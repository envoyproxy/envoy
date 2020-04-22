#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/fmt.h"

#include "extensions/filters/network/thrift_proxy/transport.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * AutoTransportImpl implements Transport and attempts to distinguish between the Thrift framed and
 * unframed transports. Once the transport is detected, subsequent operations are delegated to the
 * appropriate implementation.
 */
class AutoTransportImpl : public Transport {
public:
  AutoTransportImpl() : name_(TransportNames::get().AUTO){};

  // Transport
  const std::string& name() const override { return name_; }
  TransportType type() const override {
    if (transport_ != nullptr) {
      return transport_->type();
    }

    return TransportType::Auto;
  }
  bool decodeFrameStart(Buffer::Instance& buffer, MessageMetadata& metadata) override;
  bool decodeFrameEnd(Buffer::Instance& buffer) override;
  void encodeFrame(Buffer::Instance& buffer, const MessageMetadata& metadata,
                   Buffer::Instance& message) override;

  /*
   * Explicitly set the transport. Public to simplify testing.
   */
  void setTransport(TransportPtr&& transport) {
    transport_ = std::move(transport);
    name_ = fmt::format("{}({})", transport_->name(), TransportNames::get().AUTO);
  }

private:
  TransportPtr transport_{};
  std::string name_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
