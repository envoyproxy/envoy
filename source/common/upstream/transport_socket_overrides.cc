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
  const auto envoy_tss_itr = metadata.filter_metadata().find("envoy.transport_socket_selector");
  ENVOY_LOG(info, "incfly debug, transport socket resolving...");
  if (envoy_tss_itr == filter_metadata.end()) {
    ENVOY_LOG(info, "incfly debug, transport socket resolved default 1...");
    return *default_socket_factory_;
  }
  const auto socket_label_itr = (envoy_tss_itr->second).fields().find("label");
  if (socket_label_itr == envoy_tss_itr->second.fields().end()) {
    ENVOY_LOG(info, "incfly debug, transport socket resolved default 2...");
    return *default_socket_factory_;
  }
  const std::string& socket_label = socket_label_itr->second.string_value();
  if (socket_label == "" || socket_overrides_.find(socket_label) == socket_overrides_.end()) {
    ENVOY_LOG(info, "incfly debug, transport socket resolved default 3...");
    return *default_socket_factory_;
  }
  ENVOY_LOG(info, "incfly debug, transport socket resolved customized 1...");
  return *socket_overrides_[socket_label];
}

} // namespace Upstream
} // namespace Envoy
