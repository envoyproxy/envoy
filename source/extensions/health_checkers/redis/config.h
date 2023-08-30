#pragma once

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/redis/v3/redis.pb.h"
#include "envoy/extensions/health_checkers/redis/v3/redis.pb.validate.h"
#include "envoy/server/health_checker_config.h"

#include "source/extensions/health_checkers/redis/redis.h"

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
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.redis"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::health_checkers::redis::v3::Redis()};
  }
};

DECLARE_FACTORY(RedisHealthCheckerFactory);

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
