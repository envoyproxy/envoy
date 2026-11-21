#include "source/extensions/transport_sockets/http_11_proxy/config.h"

#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/base64.h"
#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/secret/secret_provider_impl.h"
#include "source/extensions/transport_sockets/http_11_proxy/connect.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Http11Connect {
namespace {

// Creates the transport socket factory for the given config, defaulting to raw_buffer if unset.
absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
createInnerFactory(const envoy::config::core::v3::TransportSocket* config,
                   Server::Configuration::TransportSocketFactoryContext& context) {
  auto& config_factory =
      config != nullptr ? Config::Utility::getAndCheckFactory<
                              Server::Configuration::UpstreamTransportSocketConfigFactory>(*config)
                        : Config::Utility::getAndCheckFactoryByName<
                              Server::Configuration::UpstreamTransportSocketConfigFactory>(
                              "envoy.transport_sockets.raw_buffer");
  ProtobufTypes::MessagePtr factory_config =
      config != nullptr ? Config::Utility::translateToFactoryConfig(
                              *config, context.messageValidationVisitor(), config_factory)
                        : config_factory.createEmptyConfigProto();
  return config_factory.createTransportSocketFactory(*factory_config, context);
}

absl::StatusOr<Secret::ThreadLocalGenericSecretProviderPtr> createProxyAuthorizationProvider(
    const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  Server::Configuration::ServerFactoryContext& server_context = context.serverFactoryContext();
  Secret::GenericSecretConfigProviderSharedPtr provider =
      config.has_sds_config()
          ? server_context.secretManager().findOrCreateGenericSecretProvider(
                config.sds_config(), config.name(), server_context, context.initManager(), true)
          : server_context.secretManager().findStaticGenericSecretProvider(config.name());
  if (provider == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("Unknown static secret for proxy_authorization: {}", config.name()));
  }
  return Secret::ThreadLocalGenericSecretProvider::create(
      std::move(provider), server_context.threadLocal(), server_context.api(),
      server_context.mainThreadDispatcher());
}

// Builds the Proxy-Authorization header value for Basic credentials (RFC 7617).
absl::StatusOr<std::string>
basicProxyAuthorization(const envoy::extensions::transport_sockets::http_11_proxy::v3::
                            Http11ProxyUpstreamTransport::BasicAuth& config,
                        Server::Configuration::TransportSocketFactoryContext& context) {
  auto password_or_error = Config::DataSource::read(config.password(), /*allow_empty=*/true,
                                                    context.serverFactoryContext().api());
  RETURN_IF_NOT_OK_REF(password_or_error.status());
  return absl::StrCat("Basic ",
                      Base64::encode(absl::StrCat(config.username(), ":", *password_or_error)));
}

} // namespace

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamHttp11ConnectSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {

  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport&>(
      message, context.messageValidationVisitor());

  std::optional<Network::TransportSocketOptions::Http11ProxyInfo> proxy_info;
  if (outer_config.has_default_proxy_address()) {
    auto address_or_error =
        Network::Address::resolveProtoAddress(outer_config.default_proxy_address());
    RETURN_IF_NOT_OK_REF(address_or_error.status());
    // Hostname is unknown here, so we leave it empty. It will be filled in
    // UpstreamHttp11ConnectSocket constructor from the host.
    proxy_info.emplace("", address_or_error.value());
  }

  auto factory_or_error = createInnerFactory(
      outer_config.has_transport_socket() ? &outer_config.transport_socket() : nullptr, context);
  RETURN_IF_NOT_OK_REF(factory_or_error.status());

  // Tunnel mode is enabled if the connection to the proxy has its own transport socket or the
  // proxy requires credentials.
  Network::UpstreamTransportSocketFactoryPtr proxy_socket_factory;
  Secret::ThreadLocalGenericSecretProviderPtr proxy_authorization;
  if (outer_config.has_proxy_transport_socket() || outer_config.has_proxy_authorization() ||
      outer_config.has_proxy_basic_auth()) {
    auto proxy_factory_or_error = createInnerFactory(outer_config.has_proxy_transport_socket()
                                                         ? &outer_config.proxy_transport_socket()
                                                         : nullptr,
                                                     context);
    RETURN_IF_NOT_OK_REF(proxy_factory_or_error.status());
    proxy_socket_factory = std::move(proxy_factory_or_error.value());
  }
  if (outer_config.has_proxy_authorization()) {
    auto provider_or_error =
        createProxyAuthorizationProvider(outer_config.proxy_authorization(), context);
    RETURN_IF_NOT_OK_REF(provider_or_error.status());
    proxy_authorization = std::move(provider_or_error.value());
  }
  std::string static_proxy_authorization;
  if (outer_config.has_proxy_basic_auth()) {
    auto header_or_error = basicProxyAuthorization(outer_config.proxy_basic_auth(), context);
    RETURN_IF_NOT_OK_REF(header_or_error.status());
    static_proxy_authorization = std::move(header_or_error.value());
  }

  return std::make_unique<UpstreamHttp11ConnectSocketFactory>(
      std::move(factory_or_error.value()), proxy_info, std::move(proxy_socket_factory),
      std::move(proxy_authorization), std::move(static_proxy_authorization));
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
