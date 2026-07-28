#include "contrib/istio/transport_sockets/istio_internal_upstream/source/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"

#include "contrib/envoy/extensions/transport_sockets/istio_internal_upstream/v3/istio_internal_upstream.pb.h"
#include "contrib/envoy/extensions/transport_sockets/istio_internal_upstream/v3/istio_internal_upstream.pb.validate.h"
#include "contrib/istio/transport_sockets/istio_internal_upstream/source/istio_internal_upstream.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace IstioInternalUpstream {

ProtobufTypes::MessagePtr IstioInternalUpstreamConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::istio_internal_upstream::v3::
                              IstioInternalUpstreamTransport>();
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
IstioInternalUpstreamConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::istio_internal_upstream::v3::
          IstioInternalUpstreamTransport&>(config, context.messageValidationVisitor());
  const auto& inner_upstream = outer_config.internal_upstream_transport();
  auto& inner_config_factory = Envoy::Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(
      inner_upstream.transport_socket());
  ProtobufTypes::MessagePtr inner_factory_config =
      Envoy::Config::Utility::translateToFactoryConfig(
          inner_upstream.transport_socket(), context.messageValidationVisitor(),
          inner_config_factory);
  auto factory_or_error =
      inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
  RETURN_IF_NOT_OK_REF(factory_or_error.status());
  return std::make_unique<IstioInternalSocketFactory>(
      context, inner_upstream, std::move(factory_or_error.value()));
}

REGISTER_FACTORY(IstioInternalUpstreamConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace IstioInternalUpstream
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
