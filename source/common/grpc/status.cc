#include "common/grpc/status.h"

namespace Envoy {
namespace Grpc {

Status::GrpcStatus Utility::httpToGrpcStatus(uint64_t http_response_status) {
  // From
  // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
  switch (http_response_status) {
  case 400:
    return Status::GrpcStatusMapping::Internal;
  case 401:
    return Status::GrpcStatusMapping::Unauthenticated;
  case 403:
    return Status::GrpcStatusMapping::PermissionDenied;
  case 404:
    return Status::GrpcStatusMapping::Unimplemented;
  case 429:
  case 502:
  case 503:
  case 504:
    return Status::GrpcStatusMapping::Unavailable;
  default:
    return Status::GrpcStatusMapping::Unknown;
  }
}

uint64_t Utility::grpcToHttpStatus(Status::GrpcStatus grpc_status) {
  // From https://cloud.google.com/apis/design/errors#handling_errors.
  switch (grpc_status) {
  case Status::GrpcStatusMapping::Ok:
    return 200;
  case Status::GrpcStatusMapping::Canceled:
    // Client closed request.
    return 499;
  case Status::GrpcStatusMapping::Unknown:
    // Internal server error.
    return 500;
  case Status::GrpcStatusMapping::InvalidArgument:
    // Bad request.
    return 400;
  case Status::GrpcStatusMapping::DeadlineExceeded:
    // Gateway Time-out.
    return 504;
  case Status::GrpcStatusMapping::NotFound:
    // Not found.
    return 404;
  case Status::GrpcStatusMapping::AlreadyExists:
    // Conflict.
    return 409;
  case Status::GrpcStatusMapping::PermissionDenied:
    // Forbidden.
    return 403;
  case Status::GrpcStatusMapping::ResourceExhausted:
    //  Too many requests.
    return 429;
  case Status::GrpcStatusMapping::FailedPrecondition:
    // Bad request.
    return 400;
  case Status::GrpcStatusMapping::Aborted:
    // Conflict.
    return 409;
  case Status::GrpcStatusMapping::OutOfRange:
    // Bad request.
    return 400;
  case Status::GrpcStatusMapping::Unimplemented:
    // Not implemented.
    return 501;
  case Status::GrpcStatusMapping::Internal:
    // Internal server error.
    return 500;
  case Status::GrpcStatusMapping::Unavailable:
    // Service unavailable.
    return 503;
  case Status::GrpcStatusMapping::DataLoss:
    // Internal server error.
    return 500;
  case Status::GrpcStatusMapping::Unauthenticated:
    // Unauthorized.
    return 401;
  case Status::GrpcStatusMapping::InvalidCode:
  default:
    // Internal server error.
    return 500;
  }
}

} // namespace Grpc
} // namespace Envoy
