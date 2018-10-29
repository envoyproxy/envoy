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
   * Returns the gRPC status code from a given HTTP response status code.
   * Ordinarily, it is expected that a 200 response is provided, but gRPC
   * defines a mapping for intermediaries that are not gRPC aware,
   * see https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
   *
   * Google defines a mapping where a code of 429 (rate limited) is mapped to
   * RESOURCE_EXHAUSTED instead of UNAVAILABLE as defined by gRPC. This function
   * allows the user to specify the GrpcStatus that should map to a 429 response.
   * See https://cloud.google.com/apis/design/errors#generating_errors.
   *
   * @param http_response_status HTTP status code.
   * @param grpc_status the gRPC status code to override the default mapping with.
   * @return Status::GrpcStatus corresponding gRPC status code.
   */
  static Status::GrpcStatus httpToGrpcStatus(uint64_t http_response_status,
                                             const absl::optional<Status::GrpcStatus> grpc_status);

  /**
   * @param grpc_status gRPC status from grpc-status header.
   * @return uint64_t the canonical HTTP status code corresponding to a gRPC status code.
   */
  static uint64_t grpcToHttpStatus(Status::GrpcStatus grpc_status);
};

} // namespace Grpc
} // namespace Envoy
