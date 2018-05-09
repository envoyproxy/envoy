#include "extensions/filters/network/tcp_proxy/config.h"

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/network/tcp_proxy/tcp_proxy.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

Network::FilterFactoryCb TcpProxyConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::network::tcp_proxy::v2::TcpProxy&>(proto_config),
      context);
}

Network::FilterFactoryCb
TcpProxyConfigFactory::createFilterFactory(const Json::Object& json_config,
                                           Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy proto_config;
  Config::FilterJson::translateTcpProxy(json_config, proto_config);
  return createFilter(proto_config, context);
}

ProtobufTypes::MessagePtr TcpProxyConfigFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::config::filter::network::tcp_proxy::v2::TcpProxy>(
      new envoy::config::filter::network::tcp_proxy::v2::TcpProxy());
}

std::string TcpProxyConfigFactory::name() { return NetworkFilterNames::get().TCP_PROXY; }

Network::FilterFactoryCb TcpProxyConfigFactory::createFilter(
    const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());
  if (proto_config.has_deprecated_v1()) {
    ASSERT(proto_config.deprecated_v1().routes_size() > 0);
  }

  TcpProxyConfigSharedPtr filter_config(std::make_shared<TcpProxyConfig>(proto_config, context));
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<TcpProxyFilter>(filter_config, context.clusterManager()));
  };
}

/**
 * Static registration for the tcp_proxy filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<TcpProxyConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
