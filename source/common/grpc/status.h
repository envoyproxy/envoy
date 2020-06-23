#pragma once

#include <cstdint>
#include <string>

#include "envoy/grpc/status.h"

namespace Envoy {
namespace Grpc {

/**
 * Grpc::Status utilities.
 */
class Utility {
public:
  /**
   * Returns the gRPC status code from a given HTTP response status code. Ordinarily, it is expected
   * that a 200 response is provided, but gRPC defines a mapping for intermediaries that are not
   * gRPC aware, see https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
   * @param http_response_status HTTP status code.
   * @return Status::GrpcStatus corresponding gRPC status code.
   */
  static Status::GrpcStatus httpToGrpcStatus(uint64_t http_response_status);

  /**
   * @param grpc_status gRPC status from grpc-status header.
   * @return uint64_t the canonical HTTP status code corresponding to a gRPC status code.
   */
  static uint64_t grpcToHttpStatus(Status::GrpcStatus grpc_status);

  /**
   * @param grpc_status gRPC status from grpc-status header.
   * @return gRPC status string converted from grpc-status.
   */
  static std::string grpcStatusToString(Status::GrpcStatus grpc_status);
};

} // namespace Grpc
} // namespace Envoy
