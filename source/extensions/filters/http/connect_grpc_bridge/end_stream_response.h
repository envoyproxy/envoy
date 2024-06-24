#pragma once

#include <string>
#include <vector>

#include "envoy/grpc/status.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

struct Error {
  Grpc::Status::GrpcStatus code;
  std::string message;
  std::vector<ProtobufWkt::Any> details;
};

struct EndStreamResponse {
  absl::optional<Error> error;
  absl::flat_hash_map<std::string, std::vector<std::string>> metadata;
};

/**
 * Serializes a Connect Error object to JSON.
 *
 * https://connectrpc.com/docs/protocol#error-end-stream
 *
 * @param error Error object to serialize.
 * @param out String to store result in.
 * @returns True on success, false on failure.
 */
bool serializeJson(const Error& error, std::string& out);

/**
 * Serializes a Connect EndStreamResponse object to JSON.
 *
 * https://connectrpc.com/docs/protocol#error-end-stream
 *
 * @param response EndStreamResponse object to serialize.
 * @param out String to store result in.
 * @returns True on success, false on failure.
 */
bool serializeJson(const EndStreamResponse& response, std::string& out);

uint64_t statusCodeToConnectUnaryStatus(const Grpc::Status::GrpcStatus status);

} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
