#pragma once

#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "common/common/assert.h"

#include "extensions/transport_sockets/well_known_names.h"

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

  // To be called right after contrustion.
  virtual void initialize() = 0;

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr /*options*/) const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool implementsSecureTransport() const override { return true; }
  bool usesProxyProtocolOptions() const override { return false; }

protected:
  // To be called by subclass right after construction.
  void initializeSecretUpdateCallback(Ssl::ContextConfig& context_config) {
    context_config.setSecretUpdateCallback([this]() {
      // The callback also updates config_ with the new secret.
      onSecretUpdated();
    });
  }

  virtual void onSecretUpdated() { stats_.context_config_update_by_sds_.inc(); };

  QuicTransportSocketFactoryStats stats_;

private:
};

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(Stats::Scope& store, Ssl::ServerContextConfigPtr config)
      : QuicTransportSocketFactoryBase(store, "server"), config_(std::move(config)) {}

  void initialize() override { initializeSecretUpdateCallback(*config_); }

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

private:
  Ssl::ServerContextConfigPtr config_;
};

class QuicClientTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicClientTransportSocketFactory(Stats::Scope& store, Ssl::ClientContextConfigPtr config)
      : QuicTransportSocketFactoryBase(store, "client"), config_(std::move(config)) {}

  void initialize() override { initializeSecretUpdateCallback(*config_); }

  const Ssl::ClientContextConfig& clientContextConfig() const { return *config_; }

protected:
  void onSecretUpdated() override {
    QuicTransportSocketFactoryBase::onSecretUpdated();
    // TODO(danzh) Client transport socket factory may also need to update quic crypto.
  }

private:
  Ssl::ClientContextConfigPtr config_;
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
