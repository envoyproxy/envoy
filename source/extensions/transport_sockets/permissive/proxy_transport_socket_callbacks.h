#pragma once

#include "envoy/network/io_handle.h"
#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Permissive {

class ProxyTransportSocketCallbacks : public Network::TransportSocketCallbacks {
public:
  explicit ProxyTransportSocketCallbacks(Network::TransportSocketCallbacks& parent)
      : parent_(parent) {}

  // Network::TransportSocketCallbacks
  Network::IoHandle& ioHandle() override { return parent_.ioHandle(); }
  const Network::IoHandle& ioHandle() const override { return parent_.ioHandle(); }
  Network::Connection& connection() override { return parent_.connection(); }
  bool shouldDrainReadBuffer() override { return parent_.shouldDrainReadBuffer(); }
  void setReadBufferReady() override { parent_.setReadBufferReady(); }
  void raiseEvent(Network::ConnectionEvent event) override {
    event_raised_ |= (1 << static_cast<uint32_t>(event));
    parent_.raiseEvent(event);
  }

  bool eventRaised(Network::ConnectionEvent event) {
    return (1 << static_cast<uint32_t>(event)) & event_raised_;
  }

  void clearEvents() { event_raised_ = 0; }

private:
  Network::TransportSocketCallbacks& parent_;
  uint32_t event_raised_;
};

using ProxyTransportSocketCallbacksPtr = std::unique_ptr<ProxyTransportSocketCallbacks>;

} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy