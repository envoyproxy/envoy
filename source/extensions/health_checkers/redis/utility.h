#pragma once

#include "envoy/api/v2/core/health_check.pb.validate.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

namespace {

static const envoy::config::health_checker::redis::v2::Redis
getRedisHealthCheckConfig(const envoy::api::v2::core::HealthCheck& health_check_config,
                          ProtobufMessage::ValidationVisitor& validation_visitor) {
  ProtobufTypes::MessagePtr config =
      ProtobufTypes::MessagePtr{new envoy::config::health_checker::redis::v2::Redis()};
  MessageUtil::jsonConvert(health_check_config.custom_health_check().config(), validation_visitor,
                           *config);
  return MessageUtil::downcastAndValidate<const envoy::config::health_checker::redis::v2::Redis&>(
      *config);
}

} // namespace
} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
