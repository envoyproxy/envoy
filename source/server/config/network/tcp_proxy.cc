#include "server/config/network/tcp_proxy.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/filter/tcp_proxy.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
TcpProxyConfigFactory::createFactory(const envoy::api::v2::filter::network::TcpProxy& config,
                                     FactoryContext& context) {
  Filter::TcpProxyConfigSharedPtr filter_config(new Filter::TcpProxyConfig(config, context));
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{
        new Filter::TcpProxy(filter_config, context.clusterManager())});
  };
}

NetworkFilterFactoryCb
TcpProxyConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& config,
                                                    FactoryContext& context) {
  return createFactory(dynamic_cast<const envoy::api::v2::filter::network::TcpProxy&>(config),
                       context);
}

NetworkFilterFactoryCb TcpProxyConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                                  FactoryContext& context) {
  envoy::api::v2::filter::network::TcpProxy tcp_proxy_config;
  Config::FilterJson::translateTcpProxy(json_config, tcp_proxy_config);

  return createFactory(tcp_proxy_config, context);
}

ProtobufTypes::MessagePtr TcpProxyConfigFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::api::v2::filter::network::TcpProxy>(
      new envoy::api::v2::filter::network::TcpProxy());
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<TcpProxyConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
