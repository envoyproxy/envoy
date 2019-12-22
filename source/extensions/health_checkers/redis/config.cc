#include "extensions/health_checkers/redis/config.h"

#include "envoy/api/v3alpha/core/health_check.pb.h"
#include "envoy/config/filter/network/redis_proxy/v3alpha/redis_proxy.pb.h"
#include "envoy/config/filter/network/redis_proxy/v3alpha/redis_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/health_checkers/redis/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

Upstream::HealthCheckerSharedPtr RedisHealthCheckerFactory::createCustomHealthChecker(
    const envoy::api::v3alpha::core::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<RedisHealthChecker>(
      context.cluster(), config,
      getRedisHealthCheckConfig(config, context.messageValidationVisitor()), context.dispatcher(),
      context.runtime(), context.random(), context.eventLogger(), context.api(),
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
