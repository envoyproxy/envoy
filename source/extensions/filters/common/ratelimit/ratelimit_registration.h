#pragma once

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/instance.h"
#include "envoy/singleton/manager.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

/**
 * RateLimitServiceConfig that wraps the proto structure so that it can be registered as a
 * singleton.
 */
class RateLimitServiceConfig : public Singleton::Instance {

public:
  RateLimitServiceConfig(
      const envoy::config::ratelimit::v2::RateLimitServiceConfig& ratelimit_config)
      : config_(ratelimit_config) {}

  const envoy::config::ratelimit::v2::RateLimitServiceConfig& config_;
};

typedef std::shared_ptr<RateLimitServiceConfig> RateLimitServiceConfigPtr;

/**
 * This registers the rate limit service config in the singleton manager.
 * @return RateLimitServiceConfigPtr the registered configuration.
 */
RateLimitServiceConfigPtr
registerRateLimitServiceConfig(Server::Instance& server,
                               const envoy::config::bootstrap::v2::Bootstrap& bootstrap);

/**
 * Returns the registered configuration from singleton manager.
 */
RateLimitServiceConfigPtr rateLimitConfig(Server::Configuration::FactoryContext& context);

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
