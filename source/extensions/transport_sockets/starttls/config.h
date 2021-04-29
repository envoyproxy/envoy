#pragma once

#include "envoy/extensions/transport_sockets/starttls/v3/starttls.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "common/config/utility.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

template <typename ConfigFactory, typename ConfigMessage>
class BaseStartTlsSocketFactory : public ConfigFactory {
public:
  std::string name() const override { return TransportSocketNames::get().StartTls; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigMessage>();
  }

protected:
  ConfigFactory& rawSocketConfigFactory() {
    return Config::Utility::getAndCheckFactoryByName<ConfigFactory>(
        TransportSocketNames::get().RawBuffer);
  }

  ConfigFactory& tlsSocketConfigFactory() {
    return Config::Utility::getAndCheckFactoryByName<ConfigFactory>(
        TransportSocketNames::get().Tls);
  }
};

class DownstreamStartTlsSocketFactory
    : public BaseStartTlsSocketFactory<
          Server::Configuration::DownstreamTransportSocketConfigFactory,
          envoy::extensions::transport_sockets::starttls::v3::StartTlsConfig> {
public:
  Network::TransportSocketFactoryPtr
  createTransportSocketFactory(const Protobuf::Message& config,
                               Server::Configuration::TransportSocketFactoryContext& context,
                               const std::vector<std::string>& server_names) override;
};

class UpstreamStartTlsSocketFactory
    : public BaseStartTlsSocketFactory<
          Server::Configuration::UpstreamTransportSocketConfigFactory,
          envoy::extensions::transport_sockets::starttls::v3::UpstreamStartTlsConfig> {
public:
  Network::TransportSocketFactoryPtr createTransportSocketFactory(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& context) override;
};

DECLARE_FACTORY(DownstreamStartTlsSocketFactory);
DECLARE_FACTORY(UpstreamStartTlsSocketFactory);

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
