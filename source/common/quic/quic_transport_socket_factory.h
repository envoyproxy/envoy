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
class QuicTransportSocketFactoryBase : public Network::TransportSocketFactory {
public:
  QuicTransportSocketFactoryBase(Stats::Scope& store, std::unique_ptr<Ssl::ContextConfig> config,
                                 const std::string& perspective)
      : stats_(generateStats(store, perspective)), context_config_(std::move(config)) {
    context_config_->setSecretUpdateCallback([this]() {
      // The callback also triggers updating |context_config_| with the new secret.
      // TODO(danzh) Client transport socket factory may also need to update quic crypto.
      stats_.context_config_update_by_sds_.inc();
    });
  }

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr /*options*/) const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool implementsSecureTransport() const override { return true; }
  bool usesProxyProtocolOptions() const override { return false; }

protected:
  const Ssl::ContextConfig& contextConfig() const { return *context_config_; }
  QuicTransportSocketFactoryStats stats_;

private:
  std::unique_ptr<Ssl::ContextConfig> context_config_;
};

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(Stats::Scope& store, Ssl::ServerContextConfigPtr config)
      : QuicTransportSocketFactoryBase(store, std::move(config), "server"),
        config_(dynamic_cast<const Ssl::ServerContextConfig&>(contextConfig())) {}

  const Ssl::ServerContextConfig& serverContextConfig() const { return config_; }

private:
  const Ssl::ServerContextConfig& config_;
};

class QuicClientTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicClientTransportSocketFactory(Stats::Scope& store, Ssl::ClientContextConfigPtr config)
      : QuicTransportSocketFactoryBase(store, std::move(config), "client"),
        config_(dynamic_cast<const Ssl::ClientContextConfig&>(contextConfig())) {}

  const Ssl::ClientContextConfig& clientContextConfig() const { return config_; }

private:
  const Ssl::ClientContextConfig& config_;
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
