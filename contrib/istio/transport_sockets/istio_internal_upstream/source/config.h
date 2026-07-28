#pragma once

#include "envoy/server/transport_socket_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace IstioInternalUpstream {

// Minimal extension of the core `internal_upstream` transport socket. For now
// it behaves identically to `envoy.transport_sockets.internal_upstream`; it
// exists so istio-specific behavior can be layered on later. It reuses the
// core InternalUpstreamTransport proto and InternalSocketFactory.
class IstioInternalUpstreamConfigFactory
    : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override {
    return "envoy.transport_sockets.istio_internal_upstream";
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context)
      override;
};

} // namespace IstioInternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
