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
 * Builds the rate limit client.
 */
ClientPtr rateLimitClient(Server::Configuration::FactoryContext& context,
                          const envoy::api::v2::core::GrpcService& grpc_service,
                          const std::chrono::milliseconds timeout);

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
