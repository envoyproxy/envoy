#pragma once

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
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
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return HealthCheckerNames::get().RedisHealthChecker; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::health_checker::redis::v2::Redis()};
  }
};

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
