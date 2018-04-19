#pragma once

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {

/**
 * Well-known health checker names.
 * NOTE: New health checkers should use the well known name: envoy.health_checkers.name.
 */
class HealthCheckerNameValues {
public:
  // Redis health checker.
  const std::string REDIS_HEALTH_CHECKER = "envoy.health_checkers.redis";
};

typedef ConstSingleton<HealthCheckerNameValues> HealthCheckerNames;

} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy