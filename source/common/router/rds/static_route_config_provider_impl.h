#pragma once

#include "envoy/router/rds/route_config_provider.h"
#include "envoy/server/factory_context.h"

#include "source/common/router/rds/route_config_provider_manager.h"

namespace Envoy {
namespace Router {
namespace Rds {

/**
 * Implementation of RouteConfigProvider that holds a static route configuration.
 */
template <class RouteConfiguration, class Config>
class StaticRouteConfigProviderImpl : public RouteConfigProvider<RouteConfiguration, Config> {
public:
  StaticRouteConfigProviderImpl(
      std::shared_ptr<const Config> config, const RouteConfiguration& route_config_proto,
      Server::Configuration::ServerFactoryContext& factory_context,
      RouteConfigProviderManager<RouteConfiguration, Config>& route_config_provider_manager)
      : config_(config), route_config_proto_{route_config_proto},
        last_updated_(factory_context.timeSource().systemTime()),
        route_config_provider_manager_(route_config_provider_manager) {}

  ~StaticRouteConfigProviderImpl() override {
    route_config_provider_manager_.eraseStaticProvider(this);
  }

  // Router::RouteConfigProvider
  std::shared_ptr<const Config> config() override { return config_; }
  absl::optional<typename RouteConfigProvider<RouteConfiguration, Config>::ConfigInfo>
  configInfo() const override {
    return typename RouteConfigProvider<RouteConfiguration, Config>::ConfigInfo{route_config_proto_,
                                                                                ""};
  }
  SystemTime lastUpdated() const override { return last_updated_; }
  void onConfigUpdate() override {}
  void validateConfig(const RouteConfiguration&) const override {}

private:
  std::shared_ptr<const Config> config_;
  RouteConfiguration route_config_proto_;
  SystemTime last_updated_;
  RouteConfigProviderManager<RouteConfiguration, Config>& route_config_provider_manager_;
};

} // namespace Rds
} // namespace Router
} // namespace Envoy
