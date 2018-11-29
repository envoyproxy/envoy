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

// TODO(ramaraochavali): This is added to singleton so that filters can use it to build rate limit
// clients. once we remove the rate limit service config from bootstrap, this singleton registration
// should be removed.
RateLimitServiceConfigPtr
registerRateLimitServiceConfig(Server::Instance& server,
                               const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  return server.singletonManager().getTyped<RateLimitServiceConfig>(
      SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_config), [&bootstrap] {
        return std::make_shared<RateLimitServiceConfig>(bootstrap.rate_limit_service());
      });
}

// TODO(ramaraochavali): As noted above, once we remove rate limit config from bootstrap, this
// should be deleted.
RateLimitServiceConfigPtr rateLimitConfig(Server::Configuration::FactoryContext& context) {
  return context.singletonManager().getTyped<RateLimitServiceConfig>(
      SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_config), [] {
        // This should never happen. We expect config to be registered to singleton, during
        // configuration processing in the core at start up.
        NOT_REACHED_GCOVR_EXCL_LINE;
        return nullptr;
      });
}

ClientPtr rateLimitClient(Server::Configuration::FactoryContext& context,
                          RateLimitServiceConfigPtr ratelimit_config, const uint32_t timeout_ms) {
  // When we introduce rate limit service config in filters, we should validate here that it
  // matches with bootstrap.
  ASSERT(ratelimit_config != nullptr);
  ClientFactoryPtr client_factory;
  if (ratelimit_config->config_.has_grpc_service()) {
    client_factory =
        std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::GrpcFactoryImpl>(
            ratelimit_config->config_, context.clusterManager().grpcAsyncClientManager(),
            context.scope());
  } else {
    client_factory =
        std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::NullFactoryImpl>();
  }
  return client_factory->create(std::chrono::milliseconds(timeout_ms));
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
