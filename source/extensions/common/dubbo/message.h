#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/dubbo/hessian2_utils.h"

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
using ArgumentVec = absl::InlinedVector<Hessian2::ObjectPtr, 4>;

class RequestContent : Envoy::Logger::Loggable<Envoy::Logger::Id::dubbo> {
public:
  // Initialize the content buffer with the given buffer and length.
  void initialize(Buffer::Instance& buffer, uint64_t length);

  // Initialize the content buffer with the given types and arguments and attachments.
  // The initialize() call will also encode these types and arguments into the
  // content buffer.
  void initialize(std::string&& types, ArgumentVec&& argvs, Attachments&& attachs);

  // Underlying content buffer. This may re-encode the result and attachments into the
  // content buffer to ensure the returned buffer is up-to-date.
  const Buffer::Instance& buffer();

  // Move the content buffer to the given buffer. This only does the move and does not
  // re-encode the result and attachments.
  void bufferMoveTo(Buffer::Instance& buffer);

  // Get all the arguments of the request.
  const ArgumentVec& arguments();

  // Get all the attachments of the request.
  const Attachments& attachments();

  // Set the attachment with the given key and value. If the key already exists, the
  // value will be updated. Otherwise, a new key-value pair will be added.
  void setAttachment(absl::string_view key, absl::string_view val);

  // Remove the attachment with the given key.
  void delAttachment(absl::string_view key);

private:
  // Decode the content buffer into types, arguments and attachments. The decoding is
  // lazy and will be triggered when the content is accessed.
  void lazyDecode();

  // Re-encode the attachments into the content buffer.
  void encodeAttachments();

  // Re-encode the types, arguments and attachments into the content buffer.
  void encodeEverything();

  // Called when the content is broken. The whole content will be reset to an empty
  // state.
  void handleBrokenValue();

  Buffer::OwnedImpl content_buffer_;

  // If the content has been decoded. This ensures the decoding is only performed once.
  bool decoded_{false};

  // If the attachments has been updated. This ensures the re-encoding is only
  // when the attachment has been modified.
  bool updated_{false};

  uint64_t argvs_size_{0};

  std::string types_;
  ArgumentVec argvs_;
  Attachments attachs_;
};

/**
 * RpcRequest represent an rpc call.
 */
class RpcRequest {
public:
  RpcRequest(std::string&& version, std::string&& service, std::string&& service_version,
             std::string&& method)
      : version_(std::move(version)), service_(std::move(service)),
        service_version_(std::move(service_version)), method_(std::move(method)) {}

  absl::string_view version() const { return version_; }
  absl::string_view service() const { return service_; }
  absl::string_view serviceVersion() const { return service_version_; }
  absl::string_view method() const { return method_; }

  RequestContent& content() const { return content_; }

private:
  std::string version_;
  std::string service_;
  std::string service_version_;
  std::string method_;

  mutable RequestContent content_;
};

using RpcRequestPtr = std::unique_ptr<RpcRequest>;

class ResponseContent : public Envoy::Logger::Loggable<Envoy::Logger::Id::dubbo> {
public:
  // Initialize the content buffer with the given buffer and length.
  void initialize(Buffer::Instance& buffer, uint64_t length);

  // Initialize the content buffer with the given result and attachments. The initialize()
  // call will also encode the result and attachments into the content buffer.
  void initialize(Hessian2::ObjectPtr&& value, Attachments&& attachs);

  // Underlying content buffer. This may re-encode the result and attachments into the
  // content buffer to ensure the returned buffer is up-to-date.
  const Buffer::Instance& buffer();

  // Move the content buffer to the given buffer. This only does the move and does not
  // re-encode the result and attachments.
  void bufferMoveTo(Buffer::Instance& buffer);

  // Get the result of the response. If the content has not been decoded, the decoding
  // will be triggered.
  const Hessian2::Object* result();

  // Get all the attachments of the response.
  const Attachments& attachments();

  // Set the attachment with the given key and value. If the key already exists, the
  // value will be updated. Otherwise, a new key-value pair will be added.
  void setAttachment(absl::string_view key, absl::string_view val);

  // Remove the attachment with the given key.
  void delAttachment(absl::string_view key);

private:
  // Decode the content buffer into value and attachments. The decoding is lazy and will
  // be triggered when the content is accessed.
  void lazyDecode();

  // Re-encode the attachments into the content buffer.
  void encodeAttachments();

  // Re-encode the result and attachments into the content buffer.
  void encodeEverything();

  // Called when the content is broken. The whole content will be reset to an empty
  // state.
  void handleBrokenValue();

  Buffer::OwnedImpl content_buffer_;

  // If the content has been decoded. This ensures the decoding is only performed once.
  bool decoded_{false};

  // If the attachments has been updated. This ensures the re-encoding is only
  // when the attachment has been modified.
  bool updated_{false};

  uint64_t result_size_{0};

  Hessian2::ObjectPtr result_;
  Attachments attachs_;
};

/**
 * RpcResponse represent the result of an rpc call.
 */
class RpcResponse {
public:
  RpcResponse() = default;

  void setResponseType(RpcResponseType response_type) { response_type_ = response_type; }
  absl::optional<RpcResponseType> responseType() const { return response_type_; }

  ResponseContent& content() const { return content_; }

private:
  absl::optional<RpcResponseType> response_type_{};
  mutable ResponseContent content_;
};

using RpcResponsePtr = std::unique_ptr<RpcResponse>;

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
