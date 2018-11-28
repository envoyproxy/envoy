#include "extensions/filters/common/ratelimit/ratelimit_registration.h"

#include "common/common/assert.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(ratelimit_config);

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(ratelimit_client);

// TODO(ramaraochavali): This is added to singleton so that filters can use it to build rate limit
// clients. once we remove the rate limit service config from bootstrap, this singleton registration
// should be removed.
RateLimitServiceConfigPtr
registerRateLimitServiceConfig(Server::Instance& server,
                               const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  RateLimitServiceConfigPtr ratelimit_config =
      server.singletonManager().getTyped<RateLimitServiceConfig>(
          SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_config), [&bootstrap] {
            return bootstrap.has_rate_limit_service()
                       ? std::make_shared<RateLimitServiceConfig>(bootstrap.rate_limit_service())
                       : nullptr;
          });
  return ratelimit_config;
}

// TODO(ramaraochavali): As noted above, once we remove rate limit config from bootstrap, this
// should be deleted.
RateLimitServiceConfigPtr rateLimitConfig(Server::Configuration::FactoryContext& context) {
  return context.singletonManager().getTyped<RateLimitServiceConfig>(
      SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_config), [] {
        // This should never happen. We expect config to be registered to singleton, during
        // configuration processing in the core at start up.
        ASSERT("rate limit configuration is not registered as expected");
        return nullptr;
      });
}

ClientPtr rateLimitClient(Server::Configuration::FactoryContext& context,
                          const uint32_t timeout_ms) {
  return context.singletonManager().getTyped<Client>(
      SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_client), [timeout_ms, &context] {
        // When we introduce rate limit service config in filters, we should validate here that it
        // matches with bootstrap.
        RateLimitServiceConfigPtr ratelimit_config = rateLimitConfig(context);
        ClientFactoryPtr client_factory;
        if (ratelimit_config) {
          client_factory =
              std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::GrpcFactoryImpl>(
                  ratelimit_config->config_, context.clusterManager().grpcAsyncClientManager(),
                  context.threadLocal(), context.scope());
        } else {
          client_factory =
              std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::NullFactoryImpl>();
        }
        return client_factory->create(std::chrono::milliseconds(timeout_ms));
      });
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
