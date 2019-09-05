#include "common/upstream/transport_socket_matcher.h"

namespace Envoy {
namespace Upstream {

TransportSocketMatcher::TransportSocketMatcher(
    Network::TransportSocketFactoryPtr&& socket_factory,

  TransportSocketFactoryMapPtr&& socket_factory_overrides):
  default_socket_factory_(std::move(socket_factory)),
  socket_factory_map_(std::move(socket_factory_overrides)) {}

Network::TransportSocketFactory& TransportSocketMatcher::resolve(
    const std::string& hardcode,
    const envoy::api::v2::core::Metadata& metadata) {
  if (hardcode == "127.0.0.1:9000") {
    if (socket_factory_map_->find("mtlsReady") == socket_factory_map_->end()) {
      ENVOY_LOG(info, "incfly debug, harcode transport socket resolved default 0...");
      return *default_socket_factory_;
    }
    ENVOY_LOG(info, "incfly debug, hardcode transport socket resolved customized 1...");
    return *((*socket_factory_map_)["mtlsReady"]);
  }
  const auto& filter_metadata = metadata.filter_metadata();
  const auto envoy_tss_itr = metadata.filter_metadata().find("envoy.transport_socket_selector");
  ENVOY_LOG(info, "incfly debug, transport socket resolving... {}", metadata.DebugString());
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
  if (socket_label == "" || socket_factory_map_->find(socket_label) == 
      socket_factory_map_->end()) {
    ENVOY_LOG(info, "incfly debug, transport socket resolved default 3...");
    return *default_socket_factory_;
  }
  ENVOY_LOG(info, "incfly debug, transport socket resolved customized 1...");
  return *((*socket_factory_map_)[socket_label]);
}

} // namespace Upstream
} // namespace Envoy
