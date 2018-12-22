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

ClientPtr rateLimitClient(ClientFactoryPtr client_factory,
                          Server::Configuration::FactoryContext& context,
                          const envoy::api::v2::core::GrpcService& grpc_service,
                          const std::chrono::milliseconds timeout) {
  Filters::Common::RateLimit::ClientPtr ratelimit_client;
  // If ratelimit service is defined in bootstrap, just use the factory registered to singleton,
  // otherwise create it based on the filter config.
  if (client_factory->rateLimitConfig().has_value()) {
    ratelimit_client = client_factory->create(timeout);
  } else {
    // TODO(ramaraochavali): register this factory/client to singleton when bootstrap config is
    // completely deleted.
    const auto async_client_factory =
        context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            grpc_service, context.scope(), true);
    ratelimit_client = std::make_unique<Filters::Common::RateLimit::GrpcClientImpl>(
        async_client_factory->create(), timeout);
  }
  return ratelimit_client;
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
