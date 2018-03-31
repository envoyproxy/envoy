#include "extensions/health_checkers/redis_health_checker/config.h"

#include "envoy/api/v2/core/health_check.pb.validate.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {
Upstream::HealthCheckerSharedPtr RedisHealthCheckerFactory::createExtensionHealthChecker(
    const Protobuf::Message& config, Upstream::Cluster& cluster, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher) {

  const envoy::api::v2::core::HealthCheck& hc_config =
      MessageUtil::downcastAndValidate<const envoy::api::v2::core::HealthCheck&>(config);

  return std::make_shared<RedisHealthCheckerImpl>(
      cluster, hc_config,
      MessageUtil::downcastAndValidate<const envoy::config::health_checker::redis::v2::Redis&>(
          *Config::Utility::translateToFactoryConfig(hc_config.extension_health_check(), *this)),
      dispatcher, runtime, random,
      Extensions::NetworkFilters::RedisProxy::ConnPool::ClientFactoryImpl::instance_);
};

static Registry::RegisterFactory<RedisHealthCheckerFactory, Upstream::ExtensionHealthCheckerFactory>
    registered_;

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy