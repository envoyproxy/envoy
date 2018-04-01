#pragma once

#include "envoy/server/health_checker_config.h"

#include "common/config/well_known_names.h"

#include "extensions/health_checkers/redis/redis.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

/**
 * Config registration for the redis health checker.
 */
class RedisHealthCheckerFactory : public Upstream::ExtensionHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createExtensionHealthChecker(const Protobuf::Message& config, Upstream::Cluster& cluster,
                               Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                               Event::Dispatcher& dispatcher) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::health_checker::redis::v2::Redis()};
  }

  std::string name() override { return Config::HealthCheckerNames::get().REDIS_HEALTH_CHECKER; }
};

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy