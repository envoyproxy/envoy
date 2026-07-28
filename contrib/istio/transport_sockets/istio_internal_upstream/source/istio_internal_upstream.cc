#include "contrib/istio/transport_sockets/istio_internal_upstream/source/istio_internal_upstream.h"

#include <utility>

#include "envoy/network/connection.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace IstioInternalUpstream {

IstioInternalSocket::IstioInternalSocket(
    Network::TransportSocketPtr inner_socket,
    std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
    const StreamInfo::FilterState::Objects& filter_state_objects)
    : IstioInternalSocket(std::move(inner_socket), std::move(metadata), filter_state_objects,
                          std::make_shared<UpstreamConnectionIdObject>()) {}

IstioInternalSocket::IstioInternalSocket(
    Network::TransportSocketPtr inner_socket,
    std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
    const StreamInfo::FilterState::Objects& filter_state_objects,
    std::shared_ptr<UpstreamConnectionIdObject> connection_id)
    : InternalSocket(std::move(inner_socket), std::move(metadata),
                     withConnectionId(filter_state_objects, connection_id)),
      connection_id_(std::move(connection_id)) {}

StreamInfo::FilterState::Objects IstioInternalSocket::withConnectionId(
    const StreamInfo::FilterState::Objects& objects,
    const std::shared_ptr<UpstreamConnectionIdObject>& connection_id) {
  StreamInfo::FilterState::Objects augmented = objects;
  StreamInfo::FilterState::FilterObject connection_id_object;
  connection_id_object.data_ = connection_id;
  connection_id_object.name_ = std::string(UpstreamConnectionIdFilterStateKey);
  augmented.push_back(std::move(connection_id_object));
  return augmented;
}

void IstioInternalSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  // The upstream connection ID is only available now. Write it into the shared
  // filter-state object before delegating to the base, which publishes the
  // filter-state objects (including this one) to the internal listener's
  // passthrough state.
  const uint64_t upstream_connection_id = callbacks.connection().id();
  connection_id_->set(upstream_connection_id);
  ENVOY_LOG(debug,
            "istio_internal_upstream publishing upstream connection ID (key='{}', "
            "upstream_connection_id={})",
            UpstreamConnectionIdFilterStateKey, upstream_connection_id);

  InternalSocket::setTransportSocketCallbacks(callbacks);
}

IstioInternalSocketFactory::IstioInternalSocketFactory(
    Server::Configuration::TransportSocketFactoryContext& context,
    const envoy::extensions::transport_sockets::internal_upstream::v3::InternalUpstreamTransport&
        config_proto,
    Network::UpstreamTransportSocketFactoryPtr&& inner_factory)
    : PassthroughFactory(std::move(inner_factory)), config_(config_proto, context.statsScope()) {}

Network::TransportSocketPtr IstioInternalSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr options,
    Upstream::HostDescriptionConstSharedPtr host) const {
  auto inner_socket = transport_socket_factory_->createTransportSocket(options, host);
  if (inner_socket == nullptr) {
    return nullptr;
  }
  std::unique_ptr<envoy::config::core::v3::Metadata> extracted_metadata;
  if (host) {
    extracted_metadata = config_.extractMetadata(host);
  }
  return std::make_unique<IstioInternalSocket>(
      std::move(inner_socket), std::move(extracted_metadata),
      options ? options->downstreamSharedFilterStateObjects()
              : StreamInfo::FilterState::Objects());
}

} // namespace IstioInternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
