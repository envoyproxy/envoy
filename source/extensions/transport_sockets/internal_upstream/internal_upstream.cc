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
  transport_callbacks_ = &callbacks;
  auto* io_handle = dynamic_cast<IoSocket::UserSpace::IoHandle*>(&callbacks.ioHandle());
  if (io_handle != nullptr && io_handle->passthroughState()) {
    passthrough_state_ = io_handle->passthroughState();
    passthrough_state_->initialize(std::move(metadata_), filter_state_objects_);
  }
  metadata_ = nullptr;
  filter_state_objects_.clear();
}

void InternalSocket::closeSocket(Network::ConnectionEvent event, bool abort_reset) {
  // Pull any filter state objects that the peer (inner-side) connection marked
  // SharedWithDownstreamConnectionOnClose into this (outer-side) connection's
  // stream info before the inner transport socket close runs. This makes the
  // propagated values accessible to upstream-access-log formatters and other
  // consumers on the downstream HCM side.
  if (passthrough_state_ != nullptr && transport_callbacks_ != nullptr) {
    passthrough_state_->mergeReverse(
        *transport_callbacks_->connection().streamInfo().filterState());
  }
  PassthroughSocket::closeSocket(event, abort_reset);
}

} // namespace InternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
