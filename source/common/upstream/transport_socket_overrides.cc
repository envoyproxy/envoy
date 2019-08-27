#include "common/upstream/transport_socket_overrides.h"

namespace Envoy {
namespace Upstream {

TransportSocketOverrides::TransportSocketOverrides(
    Network::TransportSocketFactoryPtr&& socket_factory,
    std::map<std::string, Network::TransportSocketFactoryPtr>&& socket_factory_overrides) :
  default_socket_factory_(std::move(socket_factory)),
  socket_overrides_(std::move(socket_factory_overrides)) {
}

Network::TransportSocketPtr TransportSocketOverrides::createTransportSocket(
    Network::TransportSocketOptionsSharedPtr options) {
  return default_socket_factory_->createTransportSocket(options);
}

Network::TransportSocketPtr TransportSocketOverrides::createTransportSocket(
    const std::string& label, Network::TransportSocketOptionsSharedPtr options) {
  if (socket_overrides_.find(label) == socket_overrides_.end()) {
    return default_socket_factory_->createTransportSocket(options);
  }
  return socket_overrides_[label]->createTransportSocket(options);
}

} // namespace Upstream
} // namespace Envoy
