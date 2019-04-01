#pragma once

#include <cstdint>

#include "envoy/grpc/status.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Grpc {

/**
 * Grpc utilities.
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
   * @param headers the headers to parse.
   * @return bool indicating whether content-type is gRPC.
   */
  static bool hasGrpcContentType(const Http::HeaderMap& headers);
};

} // namespace Grpc
} // namespace Envoy
