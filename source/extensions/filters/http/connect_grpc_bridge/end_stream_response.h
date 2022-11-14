#pragma once

#include <string>
#include <vector>

#include "envoy/grpc/status.h"

#include "source/common/buffer/buffer_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

struct ErrorDetail {
  std::string type, value;
};

struct Error {
  Grpc::Status::GrpcStatus code;
  std::string message;
  std::vector<ErrorDetail> details;
};

struct EndStreamResponse {
  absl::optional<Error> error;
  absl::flat_hash_map<std::string, std::vector<std::string>> metadata;
};

void serializeJson(const Error& error, Buffer::Instance& buffer);

void serializeJson(const EndStreamResponse& response, Buffer::Instance& buffer);

uint64_t statusCodeToConnectUnaryStatus(const Grpc::Status::GrpcStatus status);

} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
