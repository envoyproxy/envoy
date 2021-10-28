#pragma once

#include "envoy/rds/route_config_provider.h"

namespace Envoy {
namespace Rds {

/**
 * The RouteConfigProviderManager interface exposes helper functions for
 * RouteConfigProvider implementations
 */
class RouteConfigProviderManager {
public:
  virtual ~RouteConfigProviderManager() = default;

  virtual void eraseStaticProvider(RouteConfigProvider* provider) PURE;
  virtual void eraseDynamicProvider(int64_t manager_identifier) PURE;
};

} // namespace Rds
} // namespace Envoy
