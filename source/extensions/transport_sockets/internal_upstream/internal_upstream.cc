#include "source/extensions/transport_sockets/internal_upstream/internal_upstream.h"

#include "envoy/network/connection.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"


namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace InternalUpstream {

InternalSocket::InternalSocket(
    Network::TransportSocketPtr inner_socket,
    std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
    const StreamInfo::FilterState::Objects& filter_state_objects,
    const std::vector<const StreamInfo::FilterState::ObjectFactory*>& placeholder_factories)
    : PassthroughSocket(std::move(inner_socket)), metadata_(std::move(metadata)),
      filter_state_objects_(filter_state_objects), placeholder_factories_(placeholder_factories) {}

void InternalSocket::setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  callbacks_ = &callbacks;
  auto* io_handle = dynamic_cast<IoSocket::UserSpace::IoHandle*>(&callbacks.ioHandle());
  if (io_handle != nullptr && io_handle->passthroughState()) {
    // streamInfo() is not yet available on the connection here, so each created object is held
    // until onConnected() and only forward-shared at this point.
    for (const auto* factory : placeholder_factories_) {
      const std::string key = factory->name();
      bool already_present = false;
      for (const auto& obj : filter_state_objects_) {
        if (obj.name_ == key) {
          already_present = true;
          break;
        }
      }
      if (already_present) {
        continue;
      }
      std::shared_ptr<StreamInfo::FilterState::Object> placeholder = factory->createFromBytes("");
      if (placeholder == nullptr) {
        continue;
      }
      provisioned_.emplace_back(key, placeholder);
      filter_state_objects_.push_back(
          {placeholder, StreamInfo::StreamSharingMayImpactPooling::None, key});
    }
    io_handle->passthroughState()->initialize(std::move(metadata_), filter_state_objects_);
  }
  metadata_ = nullptr;
  filter_state_objects_.clear();
}

void InternalSocket::onConnected() {
  // streamInfo() is available at this point, unlike in setTransportSocketCallbacks(). The objects
  // placed here are the same instances already shared with the internal connection.
  if (callbacks_ != nullptr) {
    for (auto& [key, placeholder] : provisioned_) {
      callbacks_->connection().streamInfo().filterState()->setData(
          key, placeholder, StreamInfo::FilterState::LifeSpan::Connection);
    }
    provisioned_.clear();
  }
  PassthroughSocket::onConnected();
}

} // namespace InternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
