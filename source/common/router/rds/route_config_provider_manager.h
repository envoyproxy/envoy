#pragma once

#include "envoy/router/rds/route_config_provider.h"

namespace Envoy {
namespace Router {
namespace Rds {

/**
 * The RouteConfigProviderManager exposes the ability to get a RouteConfigProvider. This interface
 * is exposed to the Server's FactoryContext in order to allow HttpConnectionManagers to get
 * RouteConfigProviders.
 */
template <class RouteConfiguration, class Config> class RouteConfigProviderManager {
public:
  virtual ~RouteConfigProviderManager() = default;

  virtual void eraseStaticProvider(RouteConfigProvider<RouteConfiguration, Config>* provider) PURE;
  virtual void eraseDynamicProvider(int64_t manager_identifier) PURE;
};

} // namespace Rds
} // namespace Router
} // namespace Envoy
