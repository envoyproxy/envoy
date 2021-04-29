#include "extensions/filters/network/client_ssl_auth/config.h"

#include "envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.h"
#include "envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/network/client_ssl_auth/client_ssl_auth.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

Network::FilterFactoryCb ClientSslAuthConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.auth_api_cluster().empty());
  ASSERT(!proto_config.stat_prefix().empty());

  ClientSslAuthConfigSharedPtr filter_config(ClientSslAuthConfig::create(
      proto_config, context.threadLocal(), context.clusterManager(), context.dispatcher(),
      context.scope(), context.api().randomGenerator()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<ClientSslAuthFilter>(filter_config));
  };
}

/**
 * Static registration for the client SSL auth filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ClientSslAuthConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory){"envoy.client_ssl_auth"};

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
