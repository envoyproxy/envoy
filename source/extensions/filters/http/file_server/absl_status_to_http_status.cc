#include "source/extensions/filters/http/file_server/absl_status_to_http_status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

Http::Code abslStatusToHttpStatus(absl::StatusCode code) {
  switch (code) {
  case absl::StatusCode::kOk:
    return Http::Code::OK;
  case absl::StatusCode::kCancelled:
    return static_cast<Http::Code>(499);
  case absl::StatusCode::kUnknown:
    return Http::Code::InternalServerError;
  case absl::StatusCode::kInvalidArgument:
    return Http::Code::BadRequest;
  case absl::StatusCode::kDeadlineExceeded:
    return Http::Code::GatewayTimeout;
  case absl::StatusCode::kNotFound:
    return Http::Code::NotFound;
  case absl::StatusCode::kAlreadyExists:
    return Http::Code::Conflict;
  case absl::StatusCode::kPermissionDenied:
    return Http::Code::Forbidden;
  case absl::StatusCode::kResourceExhausted:
    return Http::Code::TooManyRequests;
  case absl::StatusCode::kFailedPrecondition:
    return Http::Code::BadRequest;
  case absl::StatusCode::kAborted:
    return Http::Code::Conflict;
  case absl::StatusCode::kOutOfRange:
    return Http::Code::RangeNotSatisfiable;
  case absl::StatusCode::kUnimplemented:
    return Http::Code::ServiceUnavailable;
  case absl::StatusCode::kDataLoss:
    return Http::Code::InternalServerError;
  case absl::StatusCode::kUnauthenticated:
    return Http::Code::Unauthorized;
  default:
    return Http::Code::InternalServerError;
  }
}

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
