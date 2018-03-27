#pragma once

#include "envoy/config/filter/network/client_ssl_auth/v2/client_ssl_auth.pb.validate.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

/**
 * Config registration for the client SSL auth filter. @see NamedNetworkFilterConfigFactory.
 */
class ClientSslAuthConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() override;

private:
  Server::Configuration::NetworkFilterFactoryCb createFilter(
      const envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth& proto_config,
      Server::Configuration::FactoryContext& context);
};

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
