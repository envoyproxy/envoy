#pragma once

namespace Envoy {
namespace Grpc {

class Status {
public:
  enum GrpcStatus {
    // The RPC completed successfully.
    Ok = 0,
    // The RPC was canceled.
    Canceled = 1,
    // Some unknown error occurred.
    Unknown = 2,
    // An argument to the RPC was invalid.
    InvalidArgument = 3,
    // The deadline for the RPC expired before the RPC completed.
    DeadlineExceeded = 4,
    // Some resource for the RPC was not found.
    NotFound = 5,
    // A resource the RPC attempted to create already exists.
    AlreadyExists = 6,
    // Permission was denied for the RPC.
    PermissionDenied = 7,
    // Some resource is exhausted, resulting in RPC failure.
    ResourceExhausted = 8,
    // Some precondition for the RPC failed.
    FailedPrecondition = 9,
    // The RPC was aborted.
    Aborted = 10,
    // Some operation was requested outside of a legal range.
    OutOfRange = 11,
    // The RPC requested was not implemented.
    Unimplemented = 12,
    // Some internal error occurred.
    Internal = 13,
    // The RPC endpoint is current unavailable.
    Unavailable = 14,
    // There was some data loss resulting in RPC failure.
    DataLoss = 15,
    // The RPC does not have required credentials for the RPC to succeed.
    Unauthenticated = 16,

    // This is a non-GRPC error code, indicating the status code in gRPC headers
    // was invalid.
    InvalidCode = -1,
  };
};

} // namespace Grpc
} // namespace Envoy
