#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
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
  virtual ~RequestCallbacks() {}

  /**
   * Called when a limit request is complete. The resulting status and
   * response headers are supplied.
   */
  virtual void complete(LimitStatus status, Http::HeaderMapPtr&& headers) PURE;
};

/**
 * A client used to query a centralized rate limit service.
 */
class Client {
public:
  virtual ~Client() {}

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

typedef std::unique_ptr<Client> ClientPtr;

/**
 * An interface for creating a rate limit client.
 */
class ClientFactory : public Singleton::Instance {
public:
  virtual ~ClientFactory() {}

  /**
   * Returns rate limit client from singleton manager.
   */
  virtual ClientPtr create(const absl::optional<std::chrono::milliseconds>& timeout) PURE;
};

typedef std::shared_ptr<ClientFactory> ClientFactoryPtr;

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
