#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
//#include "envoy/ratelimit/ratelimit.h"
#include "envoy/server/filter_config.h"
//#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/stats/scope.h"
//#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/grpc/typed_async_client.h"
#include "common/singleton/const_singleton.h"

//#include "extensions/filters/common/ratelimit/ratelimit.h"
#include "api/envoy/extensions/filters/network/sip_proxy/v3/load_balancer.pb.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace LoadBalancer {

using LoadBalancerAsyncCallbacks =
    Grpc::AsyncRequestCallbacks<envoy::extensions::filters::network::sip_proxy::v3::TraResponse>;

/**
 * Async callbacks used during nodes() calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  virtual void complete() PURE;
};

/**
 * A client used to query a centralized rate limit service.
 */
class Client {
public:
  virtual ~Client() = default;

  /**
   * Cancel an inflight limit request.
   */
  virtual void cancel() PURE;

  virtual void nodes(RequestCallbacks& callbacks, const std::string& fqdn,
                     Tracing::Span& parent_span) PURE;
};

using ClientPtr = std::unique_ptr<Client>;

/**
 * Builds the tra client.
 */
ClientPtr traClient(Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                    const envoy::config::core::v3::GrpcService& grpc_service,
                    const std::chrono::milliseconds timeout,
                    envoy::config::core::v3::ApiVersion transport_api_version);

} // namespace LoadBalancer
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
