#include "common/grpc/status.h"

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

} // namespace Grpc
} // namespace Envoy
