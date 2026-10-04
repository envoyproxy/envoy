#pragma once

#include "envoy/network/connection.h"

#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

inline void stopInitiatingReverseConnections(Network::Connection& connection) {
  if (connection.getSocket() == nullptr) {
    return;
  }
  auto* tunnel = dynamic_cast<Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle*>(
      &connection.getSocket()->ioHandle());
  if (tunnel != nullptr && tunnel->parent() != nullptr) {
    // Listener drain must stop replenishment before retiring any tunnel. Per-connection rotation
    // and a peer GOAWAY continue to use the replacement path while the listener is accepting.
    tunnel->parent()->stopInitiatingConnections();
  }
}

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
