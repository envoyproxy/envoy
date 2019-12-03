#include "extensions/filters/network/client_ssl_auth/config.h"

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/network/client_ssl_auth/client_ssl_auth.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

Network::FilterFactoryCb ClientSslAuthConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.auth_api_cluster().empty());
  ASSERT(!proto_config.stat_prefix().empty());

  ClientSslAuthConfigSharedPtr filter_config(
      ClientSslAuthConfig::create(proto_config, context.threadLocal(), context.clusterManager(),
                                  context.dispatcher(), context.scope(), context.random()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<ClientSslAuthFilter>(filter_config));
  };
}

/**
 * Static registration for the client SSL auth filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ClientSslAuthConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
