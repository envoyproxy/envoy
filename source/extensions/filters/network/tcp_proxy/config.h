#pragma once

#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.validate.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

/**
 * Config registration for the tcp proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class TcpProxyConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override;

  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() override;

private:
  Server::Configuration::NetworkFilterFactoryCb
  createFilter(const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& proto_config,
               Server::Configuration::FactoryContext& context);
};

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
