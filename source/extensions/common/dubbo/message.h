#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "source/common/buffer/buffer_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

/**
 * Stream reset reasons.
 */
enum class StreamResetReason : uint8_t {
  // If a local codec level reset was sent on the stream.
  LocalReset,
  // If a local codec level refused stream reset was sent on the stream (allowing for retry).
  LocalRefusedStreamReset,
  // If a remote codec level reset was received on the stream.
  RemoteReset,
  // If a remote codec level refused stream reset was received on the stream (allowing for retry).
  RemoteRefusedStreamReset,
  // If the stream was locally reset by a connection pool due to an initial connection failure.
  ConnectionFailure,
  // If the stream was locally reset due to connection termination.
  ConnectionTermination,
  // The stream was reset because of a resource overflow.
  Overflow
};

// Supported serialize type
enum class SerializeType : uint8_t {
  Hessian2 = 2,
};

// Message Type
enum class MessageType : uint8_t {
  Response = 0,
  Request = 1,
  // Special request without two-way flag.
  Oneway = 2,
  // Special response with non-Ok response status or exception response.
  Exception = 3,
  // Special request with event flag.
  HeartbeatRequest = 4,
  // Special response with event flag.
  HeartbeatResponse = 5,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST MESSAGE TYPE
  LastMessageType = HeartbeatResponse,
};

/**
 * Dubbo protocol response status types.
 * See org.apache.dubbo.remoting.exchange
 */
enum class ResponseStatus : uint8_t {
  Ok = 20,
  ClientTimeout = 30,
  ServerTimeout = 31,
  BadRequest = 40,
  BadResponse = 50,
  ServiceNotFound = 60,
  ServiceError = 70,
  ServerError = 80,
  ClientError = 90,
  ServerThreadpoolExhaustedError = 100,
};

enum class RpcResponseType : uint8_t {
  ResponseWithException = 0,
  ResponseWithValue = 1,
  ResponseWithNullValue = 2,
  ResponseWithExceptionWithAttachments = 3,
  ResponseValueWithAttachments = 4,
  ResponseNullValueWithAttachments = 5,
};

/**
 * RpcRequest represent an rpc call.
 */
class RpcRequest {
public:
  virtual ~RpcRequest() = default;

  virtual absl::string_view serviceName() const PURE;
  virtual absl::string_view methodName() const PURE;
  virtual absl::string_view serviceVersion() const PURE;
  virtual absl::optional<absl::string_view> serviceGroup() const PURE;
};

using RpcRequestPtr = std::unique_ptr<RpcRequest>;

/**
 * RpcResponse represent the result of an rpc call.
 */
class RpcResponse {
public:
  virtual ~RpcResponse() = default;

  virtual absl::optional<RpcResponseType> responseType() const PURE;
};

using RpcResponsePtr = std::unique_ptr<RpcResponse>;

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
