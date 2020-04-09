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
  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr /*options*/) const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool implementsSecureTransport() const override { return true; }
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
  QuicClientTransportSocketFactory(Envoy::Ssl::ClientContextConfigPtr config)
      : config_(std::move(config)) {}

  const Ssl::ClientContextConfig& clientContextConfig() const { return *config_; }

private:
  std::unique_ptr<const Ssl::ClientContextConfig> config_;
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
