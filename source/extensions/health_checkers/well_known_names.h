#pragma once

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {

/**
 * Well-known access logger names.
 * NOTE: New access loggers should use the well known name: envoy.access_loggers.name.
 */
class HealthCheckerNameValues {
public:
  // Redis health checker.
  const std::string REDIS_HEALTH_CHECKER = "envoy.health_checker.redis";
};

typedef ConstSingleton<HealthCheckerNameValues> HealthCheckerNames;

} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy