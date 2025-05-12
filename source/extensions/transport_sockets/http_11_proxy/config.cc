#include "source/extensions/transport_sockets/http_11_proxy/config.h"

#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/transport_sockets/http_11_proxy/connect.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamHttp11ConnectSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport&>(
      message, context.messageValidationVisitor());
  auto& inner_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(outer_config.transport_socket());
  ProtobufTypes::MessagePtr inner_factory_config = Config::Utility::translateToFactoryConfig(
      outer_config.transport_socket(), context.messageValidationVisitor(), inner_config_factory);
  auto factory_or_error =
      inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
  RETURN_IF_NOT_OK_REF(factory_or_error.status());
  return std::make_unique<UpstreamHttp11ConnectSocketFactory>(std::move(factory_or_error.value()));
}

ProtobufTypes::MessagePtr UpstreamHttp11ConnectSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport>();
}

REGISTER_FACTORY(UpstreamHttp11ConnectSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Http11Connect
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
