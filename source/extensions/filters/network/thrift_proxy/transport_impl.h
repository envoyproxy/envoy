#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/fmt.h"

#include "extensions/filters/network/thrift_proxy/transport.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/*
 * TransportImplBase provides a base class for Transport implementations.
 */
class TransportImplBase : public virtual Transport {
public:
  TransportImplBase(TransportCallbacks& callbacks) : callbacks_(callbacks) {}

protected:
  void onFrameStart(absl::optional<uint32_t> size) const { callbacks_.transportFrameStart(size); }
  void onFrameComplete() const { callbacks_.transportFrameComplete(); }

  TransportCallbacks& callbacks_;
};

/**
 * AutoTransportImpl implements Transport and attempts to distinguish between the Thrift framed and
 * unframed transports. Once the transport is detected, subsequent operations are delegated to the
 * appropriate implementation.
 */
class AutoTransportImpl : public TransportImplBase {
public:
  AutoTransportImpl(TransportCallbacks& callbacks)
      : TransportImplBase(callbacks), name_(TransportNames::get().AUTO){};

  // Transport
  const std::string& name() const override { return name_; }
  bool decodeFrameStart(Buffer::Instance& buffer) override;
  bool decodeFrameEnd(Buffer::Instance& buffer) override;
  void encodeFrame(Buffer::Instance& buffer, Buffer::Instance& message) override;

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
