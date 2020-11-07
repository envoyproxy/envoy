#pragma once

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"

#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

namespace {

static const envoy::config::health_checker::redis::v2::Redis
getRedisHealthCheckConfig(const envoy::config::core::v3::HealthCheck& health_check_config,
                          ProtobufMessage::ValidationVisitor& validation_visitor) {
  ProtobufTypes::MessagePtr config =
      ProtobufTypes::MessagePtr{new envoy::config::health_checker::redis::v2::Redis()};
  Envoy::Config::Utility::translateOpaqueConfig(
      health_check_config.custom_health_check().typed_config(),
      health_check_config.custom_health_check().hidden_envoy_deprecated_config(),
      validation_visitor, *config);
  return MessageUtil::downcastAndValidate<const envoy::config::health_checker::redis::v2::Redis&>(
      *config, validation_visitor);
}

} // namespace
} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
