#pragma once
#include "envoy/network/io_handle.h"
#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

/**
 * A TransportSocketCallbacks for wrapped TransportSocket object. Some
 * TransportSocket implementation wraps another socket which does actual I/O.
 * This class is used by the wrapped socket as its callbacks instead of the real
 * connection to hold back callbacks from the underlying socket to connection.
 */
class NoOpTransportSocketCallbacks : public Network::TransportSocketCallbacks {
public:
  explicit NoOpTransportSocketCallbacks(Network::TransportSocketCallbacks& parent)
      : parent_(parent) {}

  Network::IoHandle& ioHandle() override { return parent_.ioHandle(); }
  const Network::IoHandle& ioHandle() const override { return parent_.ioHandle(); }
  Network::Connection& connection() override { return parent_.connection(); }
  bool shouldDrainReadBuffer() override { return false; }
  /*
   * No-op for these two methods to hold back the callbacks.
   */
  void setTransportSocketIsReadable() override {}
  void raiseEvent(Network::ConnectionEvent) override {}
  void flushWriteBuffer() override {}

private:
  Network::TransportSocketCallbacks& parent_;
};

using NoOpTransportSocketCallbacksPtr = std::unique_ptr<NoOpTransportSocketCallbacks>;

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
