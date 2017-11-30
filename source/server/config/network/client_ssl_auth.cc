#include "server/config/network/client_ssl_auth.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/filter/auth/client_ssl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb ClientSslAuthConfigFactory::createFilter(
    const envoy::api::v2::filter::network::ClientSSLAuth& config, FactoryContext& context) {
  ASSERT(!config.auth_api_cluster().empty());
  ASSERT(!config.stat_prefix().empty());

  Filter::Auth::ClientSsl::ConfigSharedPtr filter_config(Filter::Auth::ClientSsl::Config::create(
      config, context.threadLocal(), context.clusterManager(), context.dispatcher(),
      context.scope(), context.random()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterSharedPtr{new Filter::Auth::ClientSsl::Instance(filter_config)});
  };
}

NetworkFilterFactoryCb
ClientSslAuthConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                FactoryContext& context) {
  envoy::api::v2::filter::network::ClientSSLAuth config;
  Config::FilterJson::translateClientSslAuthFilter(json_config, config);
  return createFilter(config, context);
}

NetworkFilterFactoryCb
ClientSslAuthConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& config,
                                                         FactoryContext& context) {
  return createFilter(dynamic_cast<const envoy::api::v2::filter::network::ClientSSLAuth&>(config),
                      context);
}

/**
 * Static registration for the client SSL auth filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ClientSslAuthConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
