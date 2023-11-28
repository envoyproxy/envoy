#include "source/common/quic/quic_transport_socket_factory.h"

#include <memory>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"

#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"

#include "quiche/quic/core/crypto/quic_client_session_cache.h"

namespace Envoy {
namespace Quic {

Network::DownstreamTransportSocketFactoryPtr
QuicServerTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& /*server_names*/) {
  auto quic_transport = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport&>(
      config, context.messageValidationVisitor());
  auto server_config = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      quic_transport.downstream_tls_context(), context);
  // TODO(RyanTheOptimist): support TLS client authentication.
  if (server_config->requireClientCertificate()) {
    throwEnvoyExceptionOrPanic("TLS Client Authentication is not supported over QUIC");
  }

  auto factory = std::make_unique<QuicServerTransportSocketFactory>(
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(quic_transport, enable_early_data, true),
      context.statsScope(), std::move(server_config));
  factory->initialize();
  return factory;
}

ProtobufTypes::MessagePtr QuicServerTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport>();
}

Network::UpstreamTransportSocketFactoryPtr
QuicClientTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  auto quic_transport = MessageUtil::downcastAndValidate<
      const envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport&>(
      config, context.messageValidationVisitor());
  auto client_config = std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
      quic_transport.upstream_tls_context(), context);
  auto factory =
      std::make_unique<QuicClientTransportSocketFactory>(std::move(client_config), context);
  factory->initialize();
  return factory;
}

QuicClientTransportSocketFactory::QuicClientTransportSocketFactory(
    Ssl::ClientContextConfigPtr config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : QuicTransportSocketFactoryBase(factory_context.statsScope(), "client"),
      fallback_factory_(std::make_unique<Extensions::TransportSockets::Tls::ClientSslSocketFactory>(
          std::move(config), factory_context.sslContextManager(), factory_context.statsScope())) {}

ProtobufTypes::MessagePtr QuicClientTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport>();
}

std::shared_ptr<quic::QuicCryptoClientConfig> QuicClientTransportSocketFactory::getCryptoConfig() {
  Envoy::Ssl::ClientContextSharedPtr context = sslCtx();
  // If the secrets haven't been loaded, there is no crypto config.
  if (context == nullptr) {
    ENVOY_LOG(warn, "SDS hasn't finished updating Ssl context config yet.");
    stats_.upstream_context_secrets_not_ready_.inc();
    return nullptr;
  }

  if (client_context_ != context) {
    // If the context has been updated, update the crypto config.
    client_context_ = context;
    crypto_config_ = std::make_shared<quic::QuicCryptoClientConfig>(
        std::make_unique<Quic::EnvoyQuicProofVerifier>(std::move(context)),
        std::make_unique<quic::QuicClientSessionCache>());
  }
  // Return the latest crypto config.
  return crypto_config_;
}

REGISTER_FACTORY(QuicServerTransportSocketConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

REGISTER_FACTORY(QuicClientTransportSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy
