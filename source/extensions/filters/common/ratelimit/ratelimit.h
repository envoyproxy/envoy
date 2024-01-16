#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/singleton/manager.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/tracer.h"

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

using DescriptorStatusList =
    std::vector<envoy::service::ratelimit::v3::RateLimitResponse_DescriptorStatus>;
using DescriptorStatusListPtr = std::unique_ptr<DescriptorStatusList>;
using DynamicMetadataPtr = std::unique_ptr<ProtobufWkt::Struct>;

/**
 * Async callbacks used during limit() calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  /**
   * Called when a limit request is complete. The resulting status, response headers
   * and request headers to be forwarded to the upstream are supplied.
   *
   * @status The ratelimit status
   * @descriptor_statuses The descriptor statuses
   * @response_headers_to_add The headers to add to the downstream response, for non-OK statuses
   * @request_headers_to_add The headers to add to the upstream request, if not ratelimited
   * @response_body The response body to use for the downstream response, for non-OK statuses. May
   * contain non UTF-8 values (e.g. binary data).
   */
  virtual void complete(LimitStatus status, DescriptorStatusListPtr&& descriptor_statuses,
                        Http::ResponseHeaderMapPtr&& response_headers_to_add,
                        Http::RequestHeaderMapPtr&& request_headers_to_add,
                        const std::string& response_body,
                        DynamicMetadataPtr&& dynamic_metadata) PURE;
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
   * @param stream_info supplies the stream info for the request.
   * @param hits_addend supplies the number of hits to add to the rate limit counter.
   *
   */
  virtual void limit(RequestCallbacks& callbacks, const std::string& domain,
                     const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                     Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info,
                     uint32_t hits_addend) PURE;
};

using ClientPtr = std::unique_ptr<Client>;

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
