#include "extensions/health_checkers/redis/config.h"

#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/health_checkers/redis/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

Upstream::HealthCheckerSharedPtr RedisHealthCheckerFactory::createCustomHealthChecker(
    const envoy::api::v2::core::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<RedisHealthChecker>(
      context.cluster(), config, getRedisHealthCheckConfig(config), context.dispatcher(),
      context.runtime(), context.random(), context.eventLogger(),
      NetworkFilters::RedisProxy::ConnPool::ClientFactoryImpl::instance_);
};

/**
 * Static registration for the redis custom health checker. @see RegisterFactory.
 */
static Registry::RegisterFactory<RedisHealthCheckerFactory,
                                 Server::Configuration::CustomHealthCheckerFactory>
    registered_;

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
