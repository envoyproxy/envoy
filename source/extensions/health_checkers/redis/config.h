#pragma once

#include "envoy/server/health_checker_config.h"

#include "extensions/health_checkers/redis/redis.h"
#include "extensions/health_checkers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

/**
 * Config registration for the redis health checker.
 */
class RedisHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::api::v2::core::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() override { return HealthCheckerNames::get().REDIS_HEALTH_CHECKER; }
};

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy