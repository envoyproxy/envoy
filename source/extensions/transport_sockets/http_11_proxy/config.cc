#include "source/extensions/transport_sockets/http_11_proxy/config.h"

#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/network/resolver_impl.h"
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
  auto inner_transport_factory =
      inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
  RETURN_IF_STATUS_NOT_OK(inner_transport_factory);

  absl::optional<std::string> proxy_address;
  if (outer_config.has_proxy_address()) {
    auto addr_instance = Network::Address::resolveProtoSocketAddress(outer_config.proxy_address());
    RETURN_IF_STATUS_NOT_OK(addr_instance);
    proxy_address = addr_instance.value()->asString();
  }

  const bool tls_exclusive = !outer_config.allow_non_tls();
  return std::make_unique<UpstreamHttp11ConnectSocketFactory>(
      std::move(inner_transport_factory.value()), proxy_address, tls_exclusive);
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
