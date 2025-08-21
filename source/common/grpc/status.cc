#include "source/common/grpc/status.h"

namespace Envoy {
namespace Grpc {

Status::GrpcStatus Utility::httpToGrpcStatus(uint64_t http_response_status) {
  // From
  // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
  switch (http_response_status) {
  case 400:
    return Status::WellKnownGrpcStatus::Internal;
  case 401:
    return Status::WellKnownGrpcStatus::Unauthenticated;
  case 403:
    return Status::WellKnownGrpcStatus::PermissionDenied;
  case 404:
    return Status::WellKnownGrpcStatus::Unimplemented;
  case 429:
  case 502:
  case 503:
  case 504:
    return Status::WellKnownGrpcStatus::Unavailable;
  default:
    return Status::WellKnownGrpcStatus::Unknown;
  }
}

uint64_t Utility::grpcToHttpStatus(Status::GrpcStatus grpc_status) {
  // From https://cloud.google.com/apis/design/errors#handling_errors.
  switch (grpc_status) {
  case Status::WellKnownGrpcStatus::Ok:
    return 200;
  case Status::WellKnownGrpcStatus::Canceled:
    // Client closed request.
    return 499;
  case Status::WellKnownGrpcStatus::Unknown:
    // Internal server error.
    return 500;
  case Status::WellKnownGrpcStatus::InvalidArgument:
    // Bad request.
    return 400;
  case Status::WellKnownGrpcStatus::DeadlineExceeded:
    // Gateway Time-out.
    return 504;
  case Status::WellKnownGrpcStatus::NotFound:
    // Not found.
    return 404;
  case Status::WellKnownGrpcStatus::AlreadyExists:
    // Conflict.
    return 409;
  case Status::WellKnownGrpcStatus::PermissionDenied:
    // Forbidden.
    return 403;
  case Status::WellKnownGrpcStatus::ResourceExhausted:
    //  Too many requests.
    return 429;
  case Status::WellKnownGrpcStatus::FailedPrecondition:
    // Bad request.
    return 400;
  case Status::WellKnownGrpcStatus::Aborted:
    // Conflict.
    return 409;
  case Status::WellKnownGrpcStatus::OutOfRange:
    // Bad request.
    return 400;
  case Status::WellKnownGrpcStatus::Unimplemented:
    // Not implemented.
    return 501;
  case Status::WellKnownGrpcStatus::Internal:
    // Internal server error.
    return 500;
  case Status::WellKnownGrpcStatus::Unavailable:
    // Service unavailable.
    return 503;
  case Status::WellKnownGrpcStatus::DataLoss:
    // Internal server error.
    return 500;
  case Status::WellKnownGrpcStatus::Unauthenticated:
    // Unauthorized.
    return 401;
  case Status::WellKnownGrpcStatus::InvalidCode:
  default:
    // Internal server error.
    return 500;
  }
}

std::string Utility::grpcStatusToString(Status::GrpcStatus grpc_status) {
  switch (grpc_status) {
  case Status::WellKnownGrpcStatus::Ok:
    return "OK";
  case Status::WellKnownGrpcStatus::Canceled:
    return "Canceled";
  case Status::WellKnownGrpcStatus::Unknown:
    return "Unknown";
  case Status::WellKnownGrpcStatus::InvalidArgument:
    return "InvalidArgument";
  case Status::WellKnownGrpcStatus::DeadlineExceeded:
    return "DeadlineExceeded";
  case Status::WellKnownGrpcStatus::NotFound:
    return "NotFound";
  case Status::WellKnownGrpcStatus::AlreadyExists:
    return "AlreadyExists";
  case Status::WellKnownGrpcStatus::PermissionDenied:
    return "PermissionDenied";
  case Status::WellKnownGrpcStatus::ResourceExhausted:
    return "ResourceExhausted";
  case Status::WellKnownGrpcStatus::FailedPrecondition:
    return "FailedPrecondition";
  case Status::WellKnownGrpcStatus::Aborted:
    return "Aborted";
  case Status::WellKnownGrpcStatus::OutOfRange:
    return "OutOfRange";
  case Status::WellKnownGrpcStatus::Unimplemented:
    return "Unimplemented";
  case Status::WellKnownGrpcStatus::Internal:
    return "Internal";
  case Status::WellKnownGrpcStatus::Unavailable:
    return "Unavailable";
  case Status::WellKnownGrpcStatus::DataLoss:
    return "DataLoss";
  case Status::WellKnownGrpcStatus::Unauthenticated:
    return "Unauthenticated";
  case Status::WellKnownGrpcStatus::InvalidCode:
  default:
    return "InvalidCode";
  }
}

} // namespace Grpc
} // namespace Envoy
