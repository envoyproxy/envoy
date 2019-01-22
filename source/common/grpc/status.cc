#include "common/grpc/status.h"

#include <string>
#include <unordered_map>

namespace Envoy {
namespace Grpc {

Status::GrpcStatus Utility::httpToGrpcStatus(uint64_t http_response_status) {
  // From
  // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
  switch (http_response_status) {
  case 400:
    return Status::GrpcStatus::Internal;
  case 401:
    return Status::GrpcStatus::Unauthenticated;
  case 403:
    return Status::GrpcStatus::PermissionDenied;
  case 404:
    return Status::GrpcStatus::Unimplemented;
  case 429:
  case 502:
  case 503:
  case 504:
    return Status::GrpcStatus::Unavailable;
  default:
    return Status::GrpcStatus::Unknown;
  }
}

uint64_t Utility::grpcToHttpStatus(Status::GrpcStatus grpc_status) {
  // From https://cloud.google.com/apis/design/errors#handling_errors.
  switch (grpc_status) {
  case Status::GrpcStatus::Ok:
    return 200;
  case Status::GrpcStatus::Canceled:
    // Client closed request.
    return 499;
  case Status::GrpcStatus::Unknown:
    // Internal server error.
    return 500;
  case Status::GrpcStatus::InvalidArgument:
    // Bad request.
    return 400;
  case Status::GrpcStatus::DeadlineExceeded:
    // Gateway Time-out.
    return 504;
  case Status::GrpcStatus::NotFound:
    // Not found.
    return 404;
  case Status::GrpcStatus::AlreadyExists:
    // Conflict.
    return 409;
  case Status::GrpcStatus::PermissionDenied:
    // Forbidden.
    return 403;
  case Status::GrpcStatus::ResourceExhausted:
    //  Too many requests.
    return 429;
  case Status::GrpcStatus::FailedPrecondition:
    // Bad request.
    return 400;
  case Status::GrpcStatus::Aborted:
    // Conflict.
    return 409;
  case Status::GrpcStatus::OutOfRange:
    // Bad request.
    return 400;
  case Status::GrpcStatus::Unimplemented:
    // Not implemented.
    return 501;
  case Status::GrpcStatus::Internal:
    // Internal server error.
    return 500;
  case Status::GrpcStatus::Unavailable:
    // Service unavailable.
    return 503;
  case Status::GrpcStatus::DataLoss:
    // Internal server error.
    return 500;
  case Status::GrpcStatus::Unauthenticated:
    // Unauthorized.
    return 401;
  case Status::GrpcStatus::InvalidCode:
  default:
    // Internal server error.
    return 500;
  }
}

absl::optional<Status::GrpcStatus> Utility::nameToGrpcStatus(const std::string& grpc_status_name) {
  // If the enum located in //include/envoy/grpc/status.h is changed, then this enum must also be
  // updated.
  const static std::unordered_map<std::string, Status::GrpcStatus> status_map({
      {"OK", Status::GrpcStatus::Ok},
      {"CANCELED", Status::GrpcStatus::Canceled},
      {"UNKNOWN", Status::GrpcStatus::Unknown},
      {"INVALID_ARGUMENT", Status::GrpcStatus::InvalidArgument},
      {"DEADLINE_EXCEEDED", Status::GrpcStatus::DeadlineExceeded},
      {"NOT_FOUND", Status::GrpcStatus::NotFound},
      {"ALREADY_EXISTS", Status::GrpcStatus::AlreadyExists},
      {"PERMISSION_DENIED", Status::GrpcStatus::PermissionDenied},
      {"RESOURCE_EXHAUSTED", Status::GrpcStatus::ResourceExhausted},
      {"FAILED_PRECONDITION", Status::GrpcStatus::FailedPrecondition},
      {"ABORTED", Status::GrpcStatus::Aborted},
      {"OUT_OF_RANGE", Status::GrpcStatus::OutOfRange},
      {"UNIMPLEMENTED", Status::GrpcStatus::Unimplemented},
      {"INTERNAL", Status::GrpcStatus::Internal},
      {"UNAVAILABLE", Status::GrpcStatus::Unavailable},
      {"DATA_LOSS", Status::GrpcStatus::DataLoss},
      {"UNAUTHENTICATED", Status::GrpcStatus::Unauthenticated},
  });

  const auto it = status_map.find(grpc_status_name);
  if (it == status_map.end()) {
    return absl::nullopt;
  } else {
    return absl::optional<Status::GrpcStatus>(it->second);
  }
}

size_t GrpcStatusHash::operator()(const Status::GrpcStatus status) const {
  return static_cast<size_t>(status);
}

} // namespace Grpc
} // namespace Envoy
