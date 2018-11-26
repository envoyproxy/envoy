#include "extensions/filters/common/ratelimit/ratelimit_registration.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(ratelimit_config);

RateLimitServiceConfigPtr
registerRateLimitServiceConfig(Server::Instance& server,
                               const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  RateLimitServiceConfigPtr ratelimit_config =
      server.singletonManager().getTyped<RateLimitServiceConfig>(
          SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_config), [&bootstrap] {
            return std::make_shared<RateLimitServiceConfig>(bootstrap.rate_limit_service());
          });
  return ratelimit_config;
}

RateLimitServiceConfigPtr rateLimitConfig(Server::Configuration::FactoryContext& context) {
  RateLimitServiceConfigPtr ratelimit_config =
      context.singletonManager().getTyped<RateLimitServiceConfig>(
          SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_config), [] { return nullptr; });
  return ratelimit_config;
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
