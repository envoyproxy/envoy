#pragma once

#include "envoy/network/transport_socket.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "common/common/assert.h"

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
 QuicTransportSocketFactoryBase(std::unique_ptr<Ssl::ContextConfig> config) 
    : context_config_(std::move(config)) {
       context_config_->setSecretUpdateCallback([]() {
         // No-op. The callback is needed to set up |config_| with the updated secret.
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
   const Ssl::ContextConfig& contextConfig() const {
     return *context_config_;
   }
private:
  std::unique_ptr<Ssl::ContextConfig> context_config_;
};

// TODO(danzh): when implement ProofSource, examine of it's necessary to
// differentiate server and client side context config.
class QuicServerTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(Ssl::ServerContextConfigPtr config) : QuicTransportSocketFactoryBase(std::move(config)), config_(dynamic_cast<const Ssl::ServerContextConfig&>(contextConfig())) {}

  const Ssl::ServerContextConfig& serverContextConfig() const { return config_; }

private:
  const Ssl::ServerContextConfig& config_;
};

class QuicClientTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicClientTransportSocketFactory(Ssl::ClientContextConfigPtr config)
      : QuicTransportSocketFactoryBase(std::move(config)), config_(dynamic_cast<const Ssl::ClientContextConfig&>(contextConfig())) {}

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

  // Prevent double registration for the config proto
  std::string configType() override { return ""; }
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

  // Prevent double registration for the config proto
  std::string configType() override { return ""; }
};

DECLARE_FACTORY(QuicClientTransportSocketConfigFactory);

} // namespace Quic
} // namespace Envoy
