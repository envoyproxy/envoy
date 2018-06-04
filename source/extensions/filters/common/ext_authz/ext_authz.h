#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/http/codes.h"
#include "envoy/service/auth/v2alpha/external_auth.pb.h"
#include "envoy/tracing/http_tracer.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

/**
 * Possible async results for a check call.
 */
enum class CheckStatus {
  // The request is authorized.
  OK,
  // The authz service could not be queried.
  Error,
  // The request is denied.
  Denied
};

/**
 * Authorization response object for a RequestCallback.
 */
struct Response {
  // Call status.
  CheckStatus status;
  // Optional http headers used on either denied or ok responses.
  Http::HeaderVector headers_to_append;
  // Optional http headers used on either denied or ok responses.
  Http::HeaderVector headers_to_add;
  // Optional http body used only on denied response.
  std::string body;
  // Optional http status used only on denied response.
  Http::Code status_code{};
};

typedef std::unique_ptr<Response> ResponsePtr;

/**
 * Async callbacks used during check() calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() {}

  /**
   * Called when a check request is complete. The resulting ResponsePtr is supplied.
   */
  virtual void onComplete(ResponsePtr&& response) PURE;
};

class Client {
public:
  // Destructor
  virtual ~Client() {}

  /**
   * Cancel an inflight Check request.
   */
  virtual void cancel() PURE;

  /**
   * Request a check call to an external authorization service which can use the
   * passed request parameters to make a permit/deny decision.
   * @param callback supplies the completion callbacks.
   *        NOTE: The callback may happen within the calling stack.
   * @param request is the proto message with the attributes of the specific payload.
   * @param parent_span source for generating an egress child span as part of the trace.
   *
   */
  virtual void check(RequestCallbacks& callback,
                     const envoy::service::auth::v2alpha::CheckRequest& request,
                     Tracing::Span& parent_span) PURE;
};

typedef std::unique_ptr<Client> ClientPtr;

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
