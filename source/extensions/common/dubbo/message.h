#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "source/common/buffer/buffer_impl.h"
#include "hessian2/object.hpp"

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

using Attachments = absl::flat_hash_map<std::string, std::string>;
using ArgumentArr = absl::InlinedVector<Hessian2::ObjectPtr, 4>;

class RequestContent : Envoy::Logger::Loggable<Envoy::Logger::Id::dubbo> {
public:
  // Initialize the content buffer with the given buffer and length.
  void initialize(Buffer::Instance& buffer, uint64_t length);

  // Initialize the content buffer with the given types and arguments and attachments.
  // The initialize() call will also encode these types and arguments into the
  // content buffer.
  void initialize(std::string&& types, ArgumentArr&& argvs, Attachments&& attachs);

  // Underlying content buffer. Never modify the buffer directly except for request
  // encoding.
  Buffer::Instance& buffer();

  // Re-encode the content buffer if the content has been modified. For example,
  // the attachment has been modified.
  void encode();

  // Get all the arguments of the request.
  // NOTE: Never modify the arguments directly except for move the ownership.
  // The whole request content should not be used after the move.
  ArgumentArr& arguments();

  // Get the argument at the given index. The index is 0-based and should be less than
  // the number of arguments. If the index is out of range, nullptr will be returned.
  const Hessian2::Object* getArgument(size_t index) const;

  // Get all the attachments of the request.
  // NOTE: Never modify the attachments directly except for move the ownership.
  // The whole request content should not be used after the move.
  Attachments& attachments();

  // Set the attachment with the given key and value. If the key already exists, the
  // value will be updated. Otherwise, a new key-value pair will be added.
  void setAttachment(absl::string_view key, absl::string_view val);

  // Get the attachment with the given key. If the key does not exist, absl::nullopt
  // will be returned.
  absl::optional<absl::string_view> getAttachment(absl::string_view key) const;

private:
  // Decode the content buffer into types, arguments and attachments. The decoding is
  // lazy and will be triggered when the content is accessed.
  void lazyDecode() const;

  void encodeAll();

  mutable Buffer::OwnedImpl content_buffer_;

  // If the content has been decoded. This ensures the decoding is only performed once.
  mutable bool decoded_{false};

  // If the content has been updated. This ensures the re-serialization is only
  // when the content has been modified.
  mutable bool updated_{false};

  mutable uint64_t argvs_size_{0};

  mutable std::string types_;
  mutable ArgumentArr argvs_;
  mutable Attachments attachs_;
};

/**
 * RpcRequest represent an rpc call.
 */
class RpcRequest {
public:
  absl::string_view service() const;
  absl::string_view method() const;
  absl::string_view version() const;
  absl::optional<absl::string_view> group() const;

  void setService(absl::string_view name);
  void setMethod(absl::string_view name);
  void setVersion(absl::string_view version);
  void setGroup(absl::string_view group);

  RequestContent& content() const;

private:
  std::string service_;
  std::string method_;
  std::string version_;
  absl::optional<std::string> group_;

  mutable RequestContent content_;
};

using RpcRequestPtr = std::unique_ptr<RpcRequest>;

class ResponseContent : public Envoy::Logger::Loggable<Envoy::Logger::Id::dubbo> {
public:
  // Initialize the content buffer with the given buffer and length.
  void initialize(Buffer::Instance& buffer, uint64_t length);

  // Initialize the content buffer with the given value and attachments. The initialize()
  // call will also encode the value and attachments into the content buffer.
  void initialize(Hessian2::ObjectPtr&& value);

  // Underlying content buffer. Never modify the buffer directly except for response
  // encoding.
  Buffer::Instance& buffer() { return content_buffer_; }

  // Re-encode the content buffer if the content has been modified. For example,
  // the attachment has been modified.
  void encode();

  // Get the value of the response. If the content has not been decoded, the decoding
  // will be triggered.
  Hessian2::Object* value() const;

  // Set the attachment with the given key and value. If the key already exists, the
  // value will be updated. Otherwise, a new key-value pair will be added.
  void setAttachment(absl::string_view key, absl::string_view val);

  // Get the attachment with the given key. If the key does not exist, absl::nullopt
  // will be returned.
  absl::string_view getAttachment(absl::string_view key) const;

private:
  // Decode the content buffer into value and attachments. The decoding is lazy and will
  // be triggered when the content is accessed.
  void lazyDecode();

  Buffer::OwnedImpl content_buffer_;

  // If the content has been decoded. This ensures the decoding is only performed once.
  bool decoded_{false};

  // If the content has been updated. This ensures the re-serialization is only
  // when the content has been modified.
  bool updated_{false};

  uint64_t value_size_{0};

  Hessian2::ObjectPtr value_;
  Attachments attachs_;
};

/**
 * RpcResponse represent the result of an rpc call.
 */
class RpcResponse {
public:
  ResponseStatus responseStatus() const;
  absl::optional<RpcResponseType> responseType() const;

  void setResponseStatus(ResponseStatus status);
  void setResponseType(RpcResponseType type);

  ResponseContent& content() const;

private:
  ResponseStatus response_status_{};
  absl::optional<RpcResponseType> response_type_{};
  mutable ResponseContent content_;
};

using RpcResponsePtr = std::unique_ptr<RpcResponse>;

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
