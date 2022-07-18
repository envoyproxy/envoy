#include "source/extensions/transport_sockets/internal_upstream/internal_upstream.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace InternalUpstream {

InternalSocket::InternalSocket(Network::TransportSocketPtr inner_socket,
                               std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                               const StreamInfo::FilterState::Objects& filter_state_objects)
    : PassthroughSocket(std::move(inner_socket)), metadata_(std::move(metadata)),
      filter_state_objects_(filter_state_objects) {}

void InternalSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  auto* io_handle = dynamic_cast<IoSocket::UserSpace::IoHandle*>(&callbacks.ioHandle());
  if (io_handle != nullptr && io_handle->passthroughState()) {
    io_handle->passthroughState()->initialize(std::move(metadata_), filter_state_objects_);
  }
  metadata_ = nullptr;
  filter_state_objects_.clear();
}

} // namespace InternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
