#pragma once

#include "envoy/api/v2/core/health_check.pb.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

namespace {

static const envoy::config::health_checker::redis::v2::Redis translateFromRedisHealthCheck(
    const envoy::api::v2::core::HealthCheck::RedisHealthCheck& deprecated_redis_config) {
  // TODO(dio): Should warn about redis_health_check depreciation here.
  envoy::config::health_checker::redis::v2::Redis config;
  config.set_key(deprecated_redis_config.key());
  return config;
}

} // namespace
} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy