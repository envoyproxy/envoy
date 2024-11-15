#pragma once

#include "envoy/extensions/transport_sockets/internal_upstream/v3/internal_upstream.pb.h"

#include "source/extensions/io_socket/user_space/io_handle.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace InternalUpstream {

class InternalSocket : public TransportSockets::PassthroughSocket {
public:
  InternalSocket(Network::TransportSocketPtr inner_socket,
                 std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                 const StreamInfo::FilterState::Objects& filter_state_objects);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;

private:
  std::unique_ptr<envoy::config::core::v3::Metadata> metadata_;
  StreamInfo::FilterState::Objects filter_state_objects_;
};

} // namespace InternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
