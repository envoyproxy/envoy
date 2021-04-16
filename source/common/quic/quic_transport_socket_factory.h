#pragma once

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "common/common/assert.h"

#include "extensions/transport_sockets/tls/ssl_socket.h"
#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Quic {

// Base class for QUIC transport socket factory.
// Because QUIC stack handles all L4 data, there is no need of a real transport
// socket for QUIC in current implementation. This factory doesn't provides a
// transport socket, instead, its derived class provides TLS context config for
// server and client.
class QuicTransportSocketFactoryBase : public Network::TransportSocketFactory {
public:
  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr /*options*/) const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool implementsSecureTransport() const override { return true; }
  bool usesProxyProtocolOptions() const override { return false; }
  bool supportsAlpn() const override { return true; }
};

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(Ssl::ServerContextConfigPtr config)
      : config_(std::move(config)) {}

  const Ssl::ServerContextConfig& serverContextConfig() const { return *config_; }

private:
  std::unique_ptr<const Ssl::ServerContextConfig> config_;
};

class QuicClientTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicClientTransportSocketFactory(
      Ssl::ClientContextConfigPtr config,
      Server::Configuration::TransportSocketFactoryContext& factory_context);

  // As documented above for QuicTransportSocketFactoryBase, the actual HTTP/3
  // code does not create transport sockets.
  // QuicClientTransportSocketFactory::createTransportSocket is called by the
  // connection grid when upstream HTTP/3 fails over to TCP, and a raw SSL socket
  // is needed. In this case the QuicClientTransportSocketFactory falls over to
  // using the fallback factory.
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override {
    return fallback_factory_->createTransportSocket(options);
  }

  // TODO(14829) make sure that clientContextConfig() is safe when secrets are updated.
  const Ssl::ClientContextConfig& clientContextConfig() const {
    return fallback_factory_->config();
  }

private:
  // The QUIC client transport socket can create TLS sockets for fallback to TCP.
  std::unique_ptr<Extensions::TransportSockets::Tls::ClientSslSocketFactory> fallback_factory_;
};

// Base class to create above QuicTransportSocketFactory for server and client
// side.
class QuicTransportSocketConfigFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  // Server::Configuration::TransportSocketConfigFactory
  std::string name() const override {
    return Extensions::TransportSockets::TransportSocketNames::get().Quic;
  }
};

class QuicServerTransportSocketConfigFactory
    : public QuicTransportSocketConfigFactory,
      public Server::Configuration::DownstreamTransportSocketConfigFactory {
public:
  // Server::Configuration::DownstreamTransportSocketConfigFactory
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;

  // Server::Configuration::TransportSocketConfigFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(QuicServerTransportSocketConfigFactory);

class QuicClientTransportSocketConfigFactory
    : public QuicTransportSocketConfigFactory,
      public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  // Server::Configuration::UpstreamTransportSocketConfigFactory
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;

  // Server::Configuration::TransportSocketConfigFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

DECLARE_FACTORY(QuicClientTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy
