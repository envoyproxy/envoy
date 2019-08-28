#include "common/upstream/transport_socket_overrides.h"

namespace Envoy {
namespace Upstream {

TransportSocketOverrides::TransportSocketOverrides(
    Network::TransportSocketFactoryPtr&& socket_factory,
    std::map<std::string, Network::TransportSocketFactoryPtr>&& socket_factory_overrides) :
  default_socket_factory_(std::move(socket_factory)),
  socket_overrides_(std::move(socket_factory_overrides)) {
}

Network::TransportSocketFactory& TransportSocketOverrides::resolve(
    const envoy::api::v2::core::Metadata& metadata) {
  // TODO(incfly): here, check ProtobufWkt::Value& Metadata::metadataValue
  const auto& filter_metadata = metadata.filter_metadata();
  if (ilter_metadata.find("hard-code-metadata-key") == filter_metadata.end()) {
    return *default_socket_factory_;
  }
  const auto& socket_label = filter_metadata["hard-code-metadata-key"];
  if (socket_label == "" || socket_overrides_.find(socket_label) == socket_overrides_.end()) {
    return *default_socket_factory_;
  }
  return *socket_overrides_[socket_label];
}

} // namespace Upstream
} // namespace Envoy
