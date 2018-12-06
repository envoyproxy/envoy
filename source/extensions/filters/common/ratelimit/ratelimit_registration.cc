#include "extensions/filters/common/ratelimit/ratelimit_registration.h"

#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(ratelimit_factory);

ClientFactoryPtr rateLimitClientFactory(Server::Instance& server,
                                        Grpc::AsyncClientManager& async_client_manager,
                                        const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  return server.singletonManager().getTyped<ClientFactory>(
      SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_factory),
      [&bootstrap, &server, &async_client_manager] {
        ClientFactoryPtr client_factory;
        if (bootstrap.rate_limit_service().has_grpc_service()) {
          client_factory =
              std::make_shared<Envoy::Extensions::Filters::Common::RateLimit::GrpcFactoryImpl>(
                  bootstrap.rate_limit_service(), async_client_manager, server.stats());
        } else {
          client_factory =
              std::make_shared<Envoy::Extensions::Filters::Common::RateLimit::NullFactoryImpl>();
        }
        return client_factory;
      });
}

ClientFactoryPtr rateLimitClientFactory(Server::Configuration::FactoryContext& context) {
  return context.singletonManager().getTyped<ClientFactory>(
      SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_factory), [] {
        // This should never happen. We expect factory to be registered to singleton, during
        // configuration processing in the core at start up.
        NOT_REACHED_GCOVR_EXCL_LINE;
        return nullptr;
      });
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
