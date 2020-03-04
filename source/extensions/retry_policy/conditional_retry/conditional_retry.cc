#include "extensions/retry_policy/conditional_retry/conditional_retry.h"

#include "envoy/router/router.h"

#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace RetryPolicy {

bool ConditionalRetry::wouldRetryHeaders(const Http::ResponseHeaderMap& response_header) {
  if (response_header.EnvoyOverloaded() != nullptr) {
    return false;
  }

  // We never retry if the request is rate limited.
  if (response_header.EnvoyRateLimited() != nullptr) {
    return false;
  }

  if (retry_on_ & Router::RetryPolicy::RETRY_ON_5XX) {
    if (Http::CodeUtility::is5xx(Http::Utility::getResponseStatus(response_header))) {
      return true;
    }
  }

  if (retry_on_ & Router::RetryPolicy::RETRY_ON_GATEWAY_ERROR) {
    if (Http::CodeUtility::isGatewayError(Http::Utility::getResponseStatus(response_header))) {
      return true;
    }
  }

  if ((retry_on_ & Router::RetryPolicy::RETRY_ON_RETRIABLE_4XX)) {
    Http::Code code = static_cast<Http::Code>(Http::Utility::getResponseStatus(response_header));
    if (code == Http::Code::Conflict) {
      return true;
    }
  }

  if ((retry_on_ & Router::RetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES)) {
    for (auto code : retriable_status_codes_) {
      if (Http::Utility::getResponseStatus(response_header) == code) {
        return true;
      }
    }
  }

  if (retry_on_ & (Router::RetryPolicy::RETRY_ON_GRPC_CANCELLED |
                   Router::RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED |
                   Router::RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED |
                   Router::RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE |
                   Router::RetryPolicy::RETRY_ON_GRPC_INTERNAL)) {
    absl::optional<Grpc::Status::GrpcStatus> status = Grpc::Common::getGrpcStatus(response_header);
    if (status) {
      if ((status.value() == Grpc::Status::Canceled &&
           (retry_on_ & Router::RetryPolicy::RETRY_ON_GRPC_CANCELLED)) ||
          (status.value() == Grpc::Status::DeadlineExceeded &&
           (retry_on_ & Router::RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED)) ||
          (status.value() == Grpc::Status::ResourceExhausted &&
           (retry_on_ & Router::RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED)) ||
          (status.value() == Grpc::Status::Unavailable &&
           (retry_on_ & Router::RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE)) ||
          (status.value() == Grpc::Status::Internal &&
           (retry_on_ & Router::RetryPolicy::RETRY_ON_GRPC_INTERNAL))) {
        return true;
      }
    }
  }

  return false;
}

bool ConditionalRetry::wouldRetryReset(Http::StreamResetReason reset_reason) {
  // First check "never retry" conditions so we can short circuit (we never
  // retry if the reset reason is overflow).
  if (reset_reason == Http::StreamResetReason::Overflow) {
    return false;
  }

  if (retry_on_ & Router::RetryPolicy::RETRY_ON_RESET) {
    return true;
  }

  if (retry_on_ &
      (Router::RetryPolicy::RETRY_ON_5XX | Router::RetryPolicy::RETRY_ON_GATEWAY_ERROR)) {
    // Currently we count an upstream reset as a "5xx" (since it will result in
    // one). With RETRY_ON_RESET we may eventually remove these policies.
    return true;
  }

  if ((retry_on_ & Router::RetryPolicy::RETRY_ON_REFUSED_STREAM) &&
      reset_reason == Http::StreamResetReason::RemoteRefusedStreamReset) {
    return true;
  }

  if ((retry_on_ & Router::RetryPolicy::RETRY_ON_CONNECT_FAILURE) &&
      reset_reason == Http::StreamResetReason::ConnectionFailure) {
    return true;
  }

  return false;
}

} // namespace RetryPolicy
} // namespace Extensions
} // namespace Envoy