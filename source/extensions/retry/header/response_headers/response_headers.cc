#include "extensions/retry/header/response_headers/response_headers.h"

#include "envoy/router/router.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Header {

bool ResponseHeadersRetryHeader::shouldRetryHeader(const Http::HeaderMap&,
                                                   const Http::HeaderMap& response_headers) {
  if (retry_on_ & Router::RetryPolicy::RETRY_ON_5XX) {
    if (Http::CodeUtility::is5xx(Http::Utility::getResponseStatus(response_headers))) {
      return true;
    }
  }

  if (retry_on_ & Router::RetryPolicy::RETRY_ON_GATEWAY_ERROR) {
    if (Http::CodeUtility::isGatewayError(Http::Utility::getResponseStatus(response_headers))) {
      return true;
    }
  }

  if ((retry_on_ & Router::RetryPolicy::RETRY_ON_RETRIABLE_4XX)) {
    Http::Code code = static_cast<Http::Code>(Http::Utility::getResponseStatus(response_headers));
    if (code == Http::Code::Conflict) {
      return true;
    }
  }

  if ((retry_on_ & Router::RetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES)) {
    for (auto code : retriable_status_codes_) {
      if (Http::Utility::getResponseStatus(response_headers) == code) {
        return true;
      }
    }
  }

  if (retry_on_ & Router::RetryPolicy::RETRY_ON_RETRIABLE_HEADERS) {
    for (const auto& retriable_header : retriable_headers_) {
      if (retriable_header->matchesHeaders(response_headers)) {
        return true;
      }
    }
  }

  if (retry_on_ & (Router::RetryPolicy::RETRY_ON_GRPC_CANCELLED |
                   Router::RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED |
                   Router::RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED |
                   Router::RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE |
                   Router::RetryPolicy::RETRY_ON_GRPC_INTERNAL)) {
    absl::optional<Grpc::Status::GrpcStatus> status = Grpc::Common::getGrpcStatus(response_headers);
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

} // namespace Header
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
