#pragma once

#include "source/common/quic/quic_transport_socket_factory.h"

namespace Envoy {
namespace Quic {

class QuicClientTransportSocketFactory : public Network::CommonUpstreamTransportSocketFactory,
                                         public QuicTransportSocketFactoryBase {
public:
  QuicClientTransportSocketFactory(
      Ssl::ClientContextConfigPtr config,
      Server::Configuration::TransportSocketFactoryContext& factory_context);

  void initialize() override {}
  bool implementsSecureTransport() const override { return true; }
  bool supportsAlpn() const override { return true; }
  absl::string_view defaultServerNameIndication() const override {
    return clientContextConfig()->serverNameIndication();
  }

  // As documented above for QuicTransportSocketFactoryBase, the actual HTTP/3
  // code does not create transport sockets.
  // QuicClientTransportSocketFactory::createTransportSocket is called by the
  // connection grid when upstream HTTP/3 fails over to TCP, and a raw SSL socket
  // is needed. In this case the QuicClientTransportSocketFactory falls over to
  // using the fallback factory.
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override {
    return fallback_factory_->createTransportSocket(options, host);
  }

  Envoy::Ssl::ClientContextSharedPtr sslCtx() override { return fallback_factory_->sslCtx(); }

  OptRef<const Ssl::ClientContextConfig> clientContextConfig() const override {
    return fallback_factory_->clientContextConfig();
  }

  // Returns a crypto config generated from the up-to-date client context config. Once the passed in
  // context config gets updated, a new crypto config object will be returned by this method.
  std::shared_ptr<quic::QuicCryptoClientConfig> getCryptoConfig() override;

protected:
  // fallback_factory_ will update the context.
  void onSecretUpdated() override {}

private:
  // The QUIC client transport socket can create TLS sockets for fallback to TCP.
  std::unique_ptr<Extensions::TransportSockets::Tls::ClientSslSocketFactory> fallback_factory_;
  // Latch the latest client context, to determine if it has updated since last
  // checked.
  Envoy::Ssl::ClientContextSharedPtr client_context_;
  // If client_context_ changes, client config will be updated as well.
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config_;
};

class QuicClientTransportSocketConfigFactory
    : public QuicTransportSocketConfigFactory,
      public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  // Server::Configuration::UpstreamTransportSocketConfigFactory
  Network::UpstreamTransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;

  // Server::Configuration::TransportSocketConfigFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(QuicClientTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy
