#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "common/buffer/buffer_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

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

// Supported protocol type
enum class ProtocolType : uint8_t {
  Dubbo = 1,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST PROTOCOL TYPE
  LastProtocolType = Dubbo,
};

// Supported serialization type
enum class SerializationType : uint8_t {
  Hessian2 = 2,
};

// Message Type
enum class MessageType : uint8_t {
  Response = 0,
  Request = 1,
  Oneway = 2,
  Exception = 3,
  HeartbeatRequest = 4,
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

class Context {
public:
  using AttachmentMap = std::unordered_map<std::string, std::string>;

  bool hasAttachments() const { return !attachments_.empty(); }
  const AttachmentMap& attachments() const { return attachments_; }

  Buffer::Instance& message_origin_data() { return message_origin_buffer_; }
  size_t message_size() const { return header_size() + body_size(); }

  virtual size_t body_size() const PURE;
  virtual size_t header_size() const PURE;

protected:
  Context() = default;
  virtual ~Context() { attachments_.clear(); }

  AttachmentMap attachments_;
  Buffer::OwnedImpl message_origin_buffer_;
};

using ContextSharedPtr = std::shared_ptr<Context>;

/**
 * RpcInvocation represent an rpc call
 * See
 * https://github.com/apache/incubator-dubbo/blob/master/dubbo-rpc/dubbo-rpc-api/src/main/java/org/apache/dubbo/rpc/RpcInvocation.java
 */
class RpcInvocation {
public:
  virtual ~RpcInvocation() = default;

  virtual const std::string& service_name() const PURE;
  virtual const std::string& method_name() const PURE;
  virtual const absl::optional<std::string>& service_version() const PURE;
  virtual const absl::optional<std::string>& service_group() const PURE;
};

using RpcInvocationSharedPtr = std::shared_ptr<RpcInvocation>;

/**
 * RpcResult represent the result of an rpc call
 * See
 * https://github.com/apache/incubator-dubbo/blob/master/dubbo-rpc/dubbo-rpc-api/src/main/java/org/apache/dubbo/rpc/RpcResult.java
 */
class RpcResult {
public:
  virtual ~RpcResult() = default;
  virtual bool hasException() const PURE;
};

using RpcResultSharedPtr = std::shared_ptr<RpcResult>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
