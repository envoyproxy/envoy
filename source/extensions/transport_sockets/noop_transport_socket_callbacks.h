#include "third_party/envoy/src/include/envoy/network/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {

/**
 * A TransportSocketCallbacks for wrapped TransportSocket object. Some
 * TransportSocket implementation wraps another socket which does actual I/O.
 * This class is used by the wrapped socket as its callbacks instead of the real
 * connection to hold back callbacks from the underlying socket to connection.
 */
class NoOpTransportSocketCallbacks : public Network::TransportSocketCallbacks {
public:
  explicit NoOpTransportSocketCallbacks(Network::TransportSocket* parent) : parent_(parent) {}

  int fd() const override { return parent_->callbacks()->fd(); }
  Network::Connection& connection() override { return parent_->callbacks()->connection(); }
  bool shouldDrainReadBuffer() override { return false; }
  /*
   * No-op for these two methods to hold back the callbacks.
   */
  void setReadBufferReady() override {}
  void raiseEvent(Network::ConnectionEvent) override {}

private:
  Network::TransportSocket* parent_;
};

}  // namespace TransportSockets
}  // namespace Extensions
}  // Envoy
