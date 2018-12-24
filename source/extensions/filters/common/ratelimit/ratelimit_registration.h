#pragma once

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/server/instance.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"
#include "extensions/filters/common/ratelimit/ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

/**
 * Builds the ClientFactory and registers with singleton manager.
 * @return ClientFactoryPtr the registered client factory.
 */
ClientFactoryPtr rateLimitClientFactory(Server::Instance& server,
                                        Grpc::AsyncClientManager& async_client_manager,
                                        const envoy::config::bootstrap::v2::Bootstrap& bootstrap);

/**
 * Returns the registered ClientFactory from singleton manager.
 */
ClientFactoryPtr rateLimitClientFactory(Server::Configuration::FactoryContext& context);

/**
 * Builds the rate limit client.
 */
ClientPtr rateLimitClient(ClientFactoryPtr ratelimit_factory,
                          Server::Configuration::FactoryContext& context,
                          const envoy::api::v2::core::GrpcService& grpc_service,
                          const std::chrono::milliseconds timeout);

/**
 * Validates the supplied filter config against the bootstrap config.
 */
template <class RateLimitProtoConfig>
void validateRateLimitConfig(RateLimitProtoConfig proto_config, ClientFactoryPtr client_factory) {
  if (proto_config.has_rate_limit_service() && client_factory->rateLimitConfig().has_value() &&
      !Envoy::Protobuf::util::MessageDifferencer::Equals(*client_factory->rateLimitConfig(),
                                                         proto_config.rate_limit_service())) {
    throw EnvoyException("rate limit service config in filter does not match with bootstrap");
  }
}
} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
