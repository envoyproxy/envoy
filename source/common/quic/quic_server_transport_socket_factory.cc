#include "source/common/quic/quic_server_transport_socket_factory.h"

#include <memory>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.validate.h"

#include "source/common/runtime/runtime_features.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"

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
    throw EnvoyException("TLS Client Authentication is not supported over QUIC");
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

void QuicServerTransportSocketFactory::initialize() {
  config_->setSecretUpdateCallback([this]() {
    // The callback also updates config_ with the new secret.
    onSecretUpdated();
  });
  if (!config_->alpnProtocols().empty()) {
    supported_alpns_ = absl::StrSplit(config_->alpnProtocols(), ',');
  }
}

REGISTER_FACTORY(QuicServerTransportSocketConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy
