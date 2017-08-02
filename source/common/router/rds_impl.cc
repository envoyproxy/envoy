#include "common/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/config/utility.h"
#include "common/json/config_schemas.h"
#include "common/router/config_impl.h"

#include "server/http_route_manager_impl.h"

namespace Envoy {
namespace Router {

RouteConfigProviderPtr RouteConfigProviderUtil::create(
    const Json::Object& config, Runtime::Loader& runtime, Upstream::ClusterManager& cm,
    Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, const std::string& stat_prefix,
    ThreadLocal::SlotAllocator& tls, Init::Manager& init_manager) {
  bool has_rds = config.hasObject("rds");
  bool has_route_config = config.hasObject("route_config");
  if (!(has_rds ^ has_route_config)) {
    throw EnvoyException(
        "http connection manager must have either rds or route_config but not both");
  }

  if (has_route_config) {
    return RouteConfigProviderPtr{
        new StaticRouteConfigProviderImpl(*config.getObject("route_config"), runtime, cm)};
  } else {
    Json::ObjectSharedPtr rds_config = config.getObject("rds");
    rds_config->validateSchema(Json::Schema::RDS_CONFIGURATION_SCHEMA);
    std::unique_ptr<Server::RdsRouteConfigProviderImpl> provider{new Server::RdsRouteConfigProviderImpl(
        *rds_config, runtime, cm, dispatcher, random, local_info, scope, stat_prefix, tls)};
    provider->registerInitTarget(init_manager);
    return std::move(provider);
  }
}

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(const Json::Object& config,
                                                             Runtime::Loader& runtime,
                                                             Upstream::ClusterManager& cm)
    : config_(new ConfigImpl(config, runtime, cm, true)) {}

} // namespace Router
} // namespace Envoy
