#include "extensions/filters/common/ratelimit/ratelimit_registration.h"

#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

ClientPtr rateLimitClient(Server::Configuration::FactoryContext& context,
                          const envoy::api::v2::core::GrpcService& grpc_service,
                          const std::chrono::milliseconds timeout) {
    const auto async_client_factory = context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            grpc_service, context.scope(), true);
    return std::make_unique<Filters::Common::RateLimit::GrpcClientImpl>(
        async_client_factory->create(), timeout);
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
