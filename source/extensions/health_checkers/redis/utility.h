#pragma once

#include "envoy/api/v2/core/health_check.pb.validate.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

namespace {

static const envoy::config::health_checker::redis::v2::Redis translateFromRedisHealthCheck(
    const envoy::api::v2::core::HealthCheck::RedisHealthCheck& deprecated_redis_config) {
  envoy::config::health_checker::redis::v2::Redis config;
  config.set_key(deprecated_redis_config.key());
  return config;
}

static const envoy::config::health_checker::redis::v2::Redis
getRedisHealthCheckConfig(const envoy::api::v2::core::HealthCheck& hc_config) {
  // TODO(dio): redis_health_check is deprecated.
  if (hc_config.has_redis_health_check()) {
    return translateFromRedisHealthCheck(hc_config.redis_health_check());
  }

  ProtobufTypes::MessagePtr config =
      ProtobufTypes::MessagePtr{new envoy::config::health_checker::redis::v2::Redis()};
  MessageUtil::jsonConvert(hc_config.custom_health_check().config(), *config);
  return MessageUtil::downcastAndValidate<const envoy::config::health_checker::redis::v2::Redis&>(
      *config);
}

} // namespace
} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy