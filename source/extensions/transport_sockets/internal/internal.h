#pragma once

#include "envoy/event/timer.h"
#include "envoy/extensions/transport_sockets/internal/v3/internal_upstream.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/host_description.h"

#include "source/extensions/io_socket/user_space/io_handle.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Internal {

class InternalSocket : public TransportSockets::PassthroughSocket {
public:
  InternalSocket(Network::TransportSocketPtr inner_socket,
                 std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                 std::unique_ptr<IoSocket::UserSpace::FilterStateObjects> filter_state_objects);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;

private:
  std::unique_ptr<envoy::config::core::v3::Metadata> metadata_;
  std::unique_ptr<IoSocket::UserSpace::FilterStateObjects> filter_state_objects_;
};

} // namespace Internal
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
