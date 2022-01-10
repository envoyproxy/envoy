#pragma once

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "source/common/common/assert.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

namespace Envoy {
namespace Quic {

#define QUIC_TRANSPORT_SOCKET_FACTORY_STATS(COUNTER)                                               \
  COUNTER(context_config_update_by_sds)                                                            \
  COUNTER(upstream_context_secrets_not_ready)                                                      \
  COUNTER(downstream_context_secrets_not_ready)

struct QuicTransportSocketFactoryStats {
  QUIC_TRANSPORT_SOCKET_FACTORY_STATS(GENERATE_COUNTER_STRUCT)
};

namespace {

QuicTransportSocketFactoryStats generateStats(Stats::Scope& store, const std::string& perspective) {
  return {QUIC_TRANSPORT_SOCKET_FACTORY_STATS(
      POOL_COUNTER_PREFIX(store, fmt::format("quic_{}_transport_socket_factory.", perspective)))};
}

} // namespace

// Base class for QUIC transport socket factory.
// Because QUIC stack handles all L4 data, there is no need of a real transport
// socket for QUIC in current implementation. This factory doesn't provides a
// transport socket, instead, its derived class provides TLS context config for
// server and client.
class QuicTransportSocketFactoryBase : public Network::TransportSocketFactory,
                                       protected Logger::Loggable<Logger::Id::quic> {
public:
  QuicTransportSocketFactoryBase(Stats::Scope& store, const std::string& perspective)
      : stats_(generateStats(store, perspective)) {}

  // To be called right after construction.
  virtual void initialize() PURE;

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr /*options*/) const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool implementsSecureTransport() const override { return true; }
  bool usesProxyProtocolOptions() const override { return false; }
  bool supportsAlpn() const override { return true; }

protected:
  virtual void onSecretUpdated() PURE;
  QuicTransportSocketFactoryStats stats_;
};

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(Stats::Scope& store, Ssl::ServerContextConfigPtr config)
      : QuicTransportSocketFactoryBase(store, "server"), config_(std::move(config)) {}

  void initialize() override {
    config_->setSecretUpdateCallback([this]() {
      // The callback also updates config_ with the new secret.
      onSecretUpdated();
    });
  }

  // Return TLS certificates if the context config is ready.
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
  getTlsCertificates() const {
    if (!config_->isReady()) {
      ENVOY_LOG(warn, "SDS hasn't finished updating Ssl context config yet.");
      stats_.downstream_context_secrets_not_ready_.inc();
      return {};
    }
    return config_->tlsCertificates();
  }

protected:
  void onSecretUpdated() override { stats_.context_config_update_by_sds_.inc(); }

private:
  Ssl::ServerContextConfigPtr config_;
};

class QuicClientTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicClientTransportSocketFactory(
      Ssl::ClientContextConfigPtr config,
      Server::Configuration::TransportSocketFactoryContext& factory_context);

  void initialize() override {}

  // As documented above for QuicTransportSocketFactoryBase, the actual HTTP/3
  // code does not create transport sockets.
  // QuicClientTransportSocketFactory::createTransportSocket is called by the
  // connection grid when upstream HTTP/3 fails over to TCP, and a raw SSL socket
  // is needed. In this case the QuicClientTransportSocketFactory falls over to
  // using the fallback factory.
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options) const override {
    return fallback_factory_->createTransportSocket(options);
  }

  virtual Envoy::Ssl::ClientContextSharedPtr sslCtx() { return fallback_factory_->sslCtx(); }

  const Ssl::ClientContextConfig& clientContextConfig() const {
    return fallback_factory_->config();
  }

protected:
  // fallback_factory_ will update the context.
  void onSecretUpdated() override {}

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
  std::string name() const override { return "envoy.transport_sockets.quic"; }
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
