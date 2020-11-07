#include "extensions/health_checkers/redis/config.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/health_checkers/redis/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

Upstream::HealthCheckerSharedPtr RedisHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<RedisHealthChecker>(
      context.cluster(), config,
      getRedisHealthCheckConfig(config, context.messageValidationVisitor()), context.dispatcher(),
      context.runtime(), context.eventLogger(), context.api(),
      NetworkFilters::Common::Redis::Client::ClientFactoryImpl::instance_);
};

/**
 * Static registration for the redis custom health checker. @see RegisterFactory.
 */
REGISTER_FACTORY(RedisHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
