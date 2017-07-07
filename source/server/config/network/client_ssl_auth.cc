#include "server/config/network/client_ssl_auth.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/filter/auth/client_ssl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
ClientSslAuthConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                FactoryContext& context) {
  Filter::Auth::ClientSsl::ConfigSharedPtr config(Filter::Auth::ClientSsl::Config::create(
      json_config, context.threadLocal(), context.clusterManager(), context.dispatcher(),
      context.scope(), context.random()));
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        Network::ReadFilterSharedPtr{new Filter::Auth::ClientSsl::Instance(config)});
  };
}

/**
 * Static registration for the client SSL auth filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ClientSslAuthConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
