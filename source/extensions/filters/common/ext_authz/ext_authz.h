#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/http/codes.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/tracer.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

/**
 * Constant values used for tracing metadata.
 */
struct TracingConstantValues {
  const std::string TraceStatus = "ext_authz_status";
  const std::string TraceUnauthz = "ext_authz_unauthorized";
  const std::string TraceOk = "ext_authz_ok";
  const std::string TraceError = "ext_authz_error";
  const std::string HttpStatus = "ext_authz_http_status";
};

using TracingConstants = ConstSingleton<TracingConstantValues>;

/**
 * Possible constant response code details values for a check call.
 */
struct ResponseCodeDetailsValues {
  // The ext_authz filter denied the downstream request/connection.
  const std::string AuthzDenied = "ext_authz_denied";
  // The ext_authz filter encountered a failure, and was configured to fail-closed.
  const std::string AuthzError = "ext_authz_error";
  // The ext_authz filter invalidated the CheckResponse and dropped the client request.
  const std::string AuthzInvalid = "ext_authz_invalid";
};
using ResponseCodeDetails = ConstSingleton<ResponseCodeDetailsValues>;

/**
 * Constant auth related HTTP headers. All lower case. This group of headers can
 * contain prefix override headers.
 */
class HeaderValues {
public:
  const char* prefix() const { return ThreadSafeSingleton<Http::PrefixValue>::get().prefix(); }

  const Http::LowerCaseString EnvoyAuthPartialBody{absl::StrCat(prefix(), "-auth-partial-body")};
  const Http::LowerCaseString EnvoyAuthHeadersToRemove{
      absl::StrCat(prefix(), "-auth-headers-to-remove")};
  const Http::LowerCaseString EnvoyAuthFailureModeAllowed{
      absl::StrCat(prefix(), "-auth-failure-mode-allowed")};
};

using Headers = ConstSingleton<HeaderValues>;

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

using UnsafeHeader = std::pair<std::string, std::string>;
using UnsafeHeaderVector = std::vector<UnsafeHeader>;

using HeaderAppendAction = envoy::config::core::v3::HeaderValueOption::HeaderAppendAction;
using HeaderValueOption = envoy::config::core::v3::HeaderValueOption;

// A single header mutation with its action type, preserving order from the proto.
struct HeaderMutation {
  std::string key;
  std::string value;
  HeaderAppendAction append_action;
  // True if this mutation came from the deprecated 'append' boolean field.
  // When true and append_action is APPEND_IF_EXISTS_OR_ADD, use appendCopy() instead
  // of addCopy() for backward compatibility.
  bool from_deprecated_append{false};
};
using HeaderMutationVector = std::vector<HeaderMutation>;

/**
 * Authorization response object for a RequestCallback.
 *
 * Header mutations and query parameter mutations are not expected to already be validated by the
 * client before packing them into this struct (hence they are "unsafe").
 */
struct Response {
  // Call status.
  CheckStatus status;
  // Ordered list of header mutations for upstream request headers on OK responses.
  // Mutations are applied in the order they appear in this vector.
  HeaderMutationVector request_header_mutations{};
  // Ordered list of header mutations for downstream response headers on OK responses.
  // Mutations are applied in the order they appear in this vector.
  HeaderMutationVector response_header_mutations{};
  // Ordered list of header mutations for local reply headers on Denied/Error responses.
  // These headers are sent back to the downstream client as part of the local reply.
  // Mutations are applied in the order they appear in this vector.
  HeaderMutationVector local_response_header_mutations{};
  // Whether the authorization server returned any headers with an invalid append action type.
  bool saw_invalid_append_actions{false};
  // A set of HTTP headers consumed by the authorization server, will be removed
  // from the request to the upstream server.
  std::vector<std::string> headers_to_remove{};
  // A set of query string parameters to be set (possibly overwritten) on the
  // request to the upstream server.
  Http::Utility::QueryParamsVector query_parameters_to_set{};
  // A set of query string parameters to remove from the request to the upstream server.
  std::vector<std::string> query_parameters_to_remove{};
  // Optional http body used only on denied response.
  std::string body{};
  // Optional http status used only on denied response.
  Http::Code status_code{};

  // A set of metadata returned by the authorization server, that will be emitted as filter's
  // dynamic metadata that other filters can leverage.
  Protobuf::Struct dynamic_metadata{};

  // The gRPC status returned by the authorization server when it is making a
  // gRPC call.
  absl::optional<Grpc::Status::GrpcStatus> grpc_status{absl::nullopt};
};

using ResponsePtr = std::unique_ptr<Response>;

/**
 * Async callbacks used during check() calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  /**
   * Called when a check request is complete. The resulting ResponsePtr is supplied.
   */
  virtual void onComplete(ResponsePtr&& response) PURE;
};

class Client {
public:
  // Destructor
  virtual ~Client() = default;

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
   * @param stream_info supplies the client's stream info.
   */
  virtual void check(RequestCallbacks& callback,
                     const envoy::service::auth::v3::CheckRequest& request,
                     Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info) PURE;

  /**
   * Returns streamInfo of the current request if possible. By default just return a nullptr.
   */
  virtual StreamInfo::StreamInfo const* streamInfo() const { return nullptr; }
};

using ClientPtr = std::unique_ptr<Client>;

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
