#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/ratelimit/v2/rls.pb.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/singleton/manager.h"
#include "envoy/tracing/http_tracer.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

/**
 * Possible async results for a limit call.
 */
enum class LimitStatus {
  // The request is not over limit.
  OK,
  // The rate limit service could not be queried.
  Error,
  // The request is over limit.
  OverLimit
};

/**
 * Async callbacks used during limit() calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  /**
   * Called when a limit request is complete. The resulting status,
   * response headers and request headers to be forwarded to the upstream are supplied.
   */
  virtual void complete(LimitStatus status, Http::HeaderMapPtr&& response_headers_to_add,
                        Http::HeaderMapPtr&& request_headers_to_add) PURE;
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

  /**
   * Request a limit check. Note that this abstract API matches the design of Lyft's GRPC based
   * rate limit service. See ratelimit.proto for details. Any other rate limit implementations
   * plugged in at this layer should support the same high level API.
   * NOTE: It is possible for the completion callback to be called immediately on the same stack
   *       frame. Calling code should account for this.
   * @param callbacks supplies the completion callbacks.
   * @param domain specifies the rate limit domain.
   * @param descriptors specifies a list of descriptors to query.
   * @param parent_span source for generating an egress child span as part of the trace.
   *
   */
  virtual void limit(RequestCallbacks& callbacks, const std::string& domain,
                     const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                     Tracing::Span& parent_span) PURE;
};

using ClientPtr = std::unique_ptr<Client>;

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
