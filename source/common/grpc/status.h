#pragma once

#include <cstdint>

#include "envoy/grpc/status.h"

#include "absl/types/optional.h"

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
   * Returns the gRPC status code from its name, as represented by a snake_case string.
   * @param grpc_status_name The name of the gRPC status.
   * @return absl::optional<Status::GrpcStatus> The corresponding gRPC status code, if one exists.
   */
  static absl::optional<Status::GrpcStatus> nameToGrpcStatus(const std::string& grpc_status_name);
};

} // namespace Grpc
} // namespace Envoy
