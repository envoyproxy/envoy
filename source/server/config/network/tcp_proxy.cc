#include "server/config/network/tcp_proxy.h"

#include <string>

#include "envoy/api/v2/filter/network/tcp_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/filter/tcp_proxy.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
TcpProxyConfigFactory::createFilter(const envoy::api::v2::filter::network::TcpProxy& proto_config,
                                    FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());
  if (proto_config.has_deprecated_v1()) {
    ASSERT(proto_config.deprecated_v1().routes_size() > 0);
  }

  Filter::TcpProxyConfigSharedPtr filter_config(new Filter::TcpProxyConfig(proto_config, context));
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{
        new Filter::TcpProxy(filter_config, context.clusterManager())});
  };
}

NetworkFilterFactoryCb TcpProxyConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                                  FactoryContext& context) {
  envoy::api::v2::filter::network::TcpProxy proto_config;
  Config::FilterJson::translateTcpProxy(json_config, proto_config);
  return createFilter(proto_config, context);
}

NetworkFilterFactoryCb
TcpProxyConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                    FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::api::v2::filter::network::TcpProxy&>(
          proto_config),
      context);
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<TcpProxyConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
