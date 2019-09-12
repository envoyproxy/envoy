#include "envoy/server/transport_socket_config.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context_config.h"

#include "common/common/assert.h"
#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Quic {

class QuicTransportSocketFactoryBase : public Network::TransportSocketFactory {
public:
  ~QuicTransportSocketFactoryBase() override = default;

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr /*options*/) const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool implementsSecureTransport() const override {
    return true;
  }
};

class QuicServerTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicServerTransportSocketFactory(Envoy::Ssl::ServerContextConfigPtr config) : config_(std::move(config)) {}

  Ssl::ServerContextConfig& serverContextConfig() {
    return *config_;
  }

private:
   Ssl::ServerContextConfigPtr config_;
};

class QuicClientTransportSocketFactory : public QuicTransportSocketFactoryBase {
public:
  QuicClientTransportSocketFactory(Envoy::Ssl::ClientContextConfigPtr config) : config_(std::move(config)) {}

  Ssl::ClientContextConfig& clientContextConfig() {
    return *config_;
  }

private:
   Ssl::ClientContextConfigPtr config_;
};

class QuicTransportSocketConfigFactory
    : public virtual Server::Configuration::TransportSocketConfigFactory {
public:
  ~QuicTransportSocketConfigFactory() override = default;
  std::string name() const override { return Extensions::TransportSockets::TransportSocketNames::get().Quic; }
};

class QuicServerTransportSocketConfigFactory
    : public QuicTransportSocketConfigFactory,
      public Server::Configuration::DownstreamTransportSocketConfigFactory {
public:
  // Server::Configuration::DownstreamTransportSocketConfigFactory
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
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

}  // namespace Quic
}  // namespace Envoy
