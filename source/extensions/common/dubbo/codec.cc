#include "source/extensions/common/dubbo/codec.h"

#include <cstdint>
#include <memory>

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/extensions/common/dubbo/hessian2_serializer_impl.h"
#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

namespace {

constexpr uint16_t MagicNumber = 0xdabb;
constexpr uint8_t MessageTypeMask = 0x80;
constexpr uint8_t EventMask = 0x20;
constexpr uint8_t TwoWayMask = 0x40;
constexpr uint8_t SerializeTypeMask = 0x1f;
constexpr uint64_t FlagOffset = 2;
constexpr uint64_t StatusOffset = 3;
constexpr uint64_t RequestIDOffset = 4;
constexpr uint64_t BodySizeOffset = 12;

void encodeHeader(Buffer::Instance& buffer, Context& context, uint32_t body_size) {
  // Magic number.
  buffer.writeBEInt<uint16_t>(MagicNumber);

  // Serialize type and flag.
  uint8_t flag = static_cast<uint8_t>(SerializeType::Hessian2);

  switch (context.messageType()) {
  case MessageType::Response:
    // Normal response
    break;
  case MessageType::Request:
    // Normal request.
    flag ^= MessageTypeMask;
    flag ^= TwoWayMask;
    break;
  case MessageType::Oneway:
    // Oneway request.
    flag ^= MessageTypeMask;
    break;
  case MessageType::Exception:
    // Exception response.
    break;
  case MessageType::HeartbeatRequest:
    // Event request.
    flag ^= MessageTypeMask;
    flag ^= TwoWayMask;
    flag ^= EventMask;
    break;
  case MessageType::HeartbeatResponse:
    flag ^= EventMask;
    break;
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
  buffer.writeByte(flag);

  // Optional response status.
  buffer.writeByte(context.hasResponseStatus() ? static_cast<uint8_t>(context.responseStatus())
                                               : 0x00);

  // Request id.
  buffer.writeBEInt<uint64_t>(context.requestId());

  // Because the body size in the context is the size of original request or response.
  // It may be changed after the processing of filters. So write the explicit specified
  // body size here.
  buffer.writeBEInt<uint32_t>(body_size);
}

} // namespace

// Consistent with the SerializeType
bool isValidSerializeType(SerializeType type) {
  switch (type) {
  case SerializeType::Hessian2:
    break;
  default:
    return false;
  }
  return true;
}

// Consistent with the ResponseStatus
bool isValidResponseStatus(ResponseStatus status) {
  switch (status) {
  case ResponseStatus::Ok:
  case ResponseStatus::ClientTimeout:
  case ResponseStatus::ServerTimeout:
  case ResponseStatus::BadRequest:
  case ResponseStatus::BadResponse:
  case ResponseStatus::ServiceNotFound:
  case ResponseStatus::ServiceError:
  case ResponseStatus::ServerError:
  case ResponseStatus::ClientError:
  case ResponseStatus::ServerThreadpoolExhaustedError:
    return true;
  }
  return false;
}

void parseRequestInfoFromBuffer(Buffer::Instance& data, Context& context) {
  ASSERT(data.length() >= DubboCodec::HeadersSize);
  uint8_t flag = data.peekInt<uint8_t>(FlagOffset);
  bool is_two_way = (flag & TwoWayMask) == TwoWayMask ? true : false;

  // Request without two flag should be one way request.
  if (!is_two_way && context.messageType() != MessageType::HeartbeatRequest) {
    context.setMessageType(MessageType::Oneway);
  }
}

void parseResponseInfoFromBuffer(Buffer::Instance& buffer, Context& context) {
  ASSERT(buffer.length() >= DubboCodec::HeadersSize);
  ResponseStatus status = static_cast<ResponseStatus>(buffer.peekInt<uint8_t>(StatusOffset));
  if (!isValidResponseStatus(status)) {
    throw EnvoyException(
        absl::StrCat("invalid dubbo message response status ",
                     static_cast<std::underlying_type<ResponseStatus>::type>(status)));
  }
  context.setResponseStatus(status);

  if (status != ResponseStatus::Ok) {
    context.setMessageType(MessageType::Exception);
  }
}

DubboCodecPtr DubboCodec::codecFromSerializeType(SerializeType type) {
  ASSERT(type == SerializeType::Hessian2);

  auto codec = std::make_unique<DubboCodec>();
  codec->initilize(std::make_unique<Hessian2SerializerImpl>());
  return codec;
}

DecodeStatus DubboCodec::decodeHeader(Buffer::Instance& buffer, MessageMetadata& metadata) {
  // Empty metadata.
  ASSERT(!metadata.hasContext());

  if (buffer.length() < DubboCodec::HeadersSize) {
    return DecodeStatus::Waiting;
  }

  uint16_t magic_number = buffer.peekBEInt<uint16_t>();
  if (magic_number != MagicNumber) {
    throw EnvoyException(absl::StrCat("invalid dubbo message magic number ", magic_number));
  }

  auto context = std::make_unique<Context>();

  uint8_t flag = buffer.peekInt<uint8_t>(FlagOffset);

  // Decode serialize type.
  SerializeType serialize_type = static_cast<SerializeType>(flag & SerializeTypeMask);
  if (!isValidSerializeType(serialize_type)) {
    throw EnvoyException(
        absl::StrCat("invalid dubbo message serialization type ",
                     static_cast<std::underlying_type<SerializeType>::type>(serialize_type)));
  }

  // Initial basic type of message.
  MessageType type =
      (flag & MessageTypeMask) == MessageTypeMask ? MessageType::Request : MessageType::Response;

  bool is_event = (flag & EventMask) == EventMask ? true : false;

  int64_t request_id = buffer.peekBEInt<int64_t>(RequestIDOffset);

  int32_t body_size = buffer.peekBEInt<int32_t>(BodySizeOffset);

  // The body size of the heartbeat message is zero.
  if (body_size > MaxBodySize || body_size < 0) {
    throw EnvoyException(absl::StrCat("invalid dubbo message size ", body_size));
  }

  context->setRequestId(request_id);

  if (type == MessageType::Request) {
    if (is_event) {
      type = MessageType::HeartbeatRequest;
    }
    context->setMessageType(type);
    parseRequestInfoFromBuffer(buffer, *context);
  } else {
    if (is_event) {
      type = MessageType::HeartbeatResponse;
    }
    context->setMessageType(type);
    parseResponseInfoFromBuffer(buffer, *context);
  }

  context->setBodySize(body_size);

  metadata.setContext(std::move(context));

  // Drain headers bytes.
  buffer.drain(DubboCodec::HeadersSize);
  return DecodeStatus::Success;
}

DecodeStatus DubboCodec::decodeData(Buffer::Instance& buffer, MessageMetadata& metadata) {
  ASSERT(metadata.hasContext());
  ASSERT(serializer_ != nullptr);

  auto& context = metadata.mutableContext();

  if (buffer.length() < context.bodySize()) {
    return DecodeStatus::Waiting;
  }

  switch (context.messageType()) {
  case MessageType::Response:
  case MessageType::Exception:
  case MessageType::HeartbeatResponse:
    // Handle response.
    metadata.setResponse(serializer_->deserializeRpcResponse(buffer, context));
    break;
  case MessageType::Request:
  case MessageType::Oneway:
  case MessageType::HeartbeatRequest:
    // Handle request.
    metadata.setRequest(serializer_->deserializeRpcRequest(buffer, context));
    break;
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  return DecodeStatus::Success;
}

void DubboCodec::encode(Buffer::Instance& buffer, MessageMetadata& metadata) {
  ASSERT(metadata.hasContext());
  ASSERT(serializer_);

  auto& context = metadata.mutableContext();

  Buffer::OwnedImpl body_buffer;

  switch (context.messageType()) {
  case MessageType::Response:
  case MessageType::Exception:
  case MessageType::HeartbeatResponse:
    serializer_->serializeRpcResponse(body_buffer, metadata);
    break;
  case MessageType::Request:
  case MessageType::Oneway:
  case MessageType::HeartbeatRequest:
    serializer_->serializeRpcRequest(body_buffer, metadata);
    break;
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  encodeHeader(buffer, context, body_buffer.length());
  buffer.move(body_buffer);
}

void DubboCodec::encodeHeaderForTest(Buffer::Instance& buffer, Context& context) {
  encodeHeader(buffer, context, context.bodySize());
}

MessageMetadataSharedPtr DirectResponseUtil::heartbeatResponse(MessageMetadata& heartbeat_request) {
  ASSERT(heartbeat_request.hasContext());
  ASSERT(heartbeat_request.messageType() == MessageType::HeartbeatRequest);
  const auto& request_context = heartbeat_request.context();
  auto context = std::make_unique<Context>();

  // Set context.
  context->setMessageType(MessageType::HeartbeatResponse);
  context->setResponseStatus(ResponseStatus::Ok);
  context->setRequestId(request_context.requestId());

  auto metadata = std::make_shared<MessageMetadata>();
  metadata->setContext(std::move(context));
  return metadata;
}

MessageMetadataSharedPtr DirectResponseUtil::localResponse(MessageMetadata& request,
                                                           ResponseStatus status,
                                                           absl::optional<RpcResponseType> type,
                                                           absl::string_view content) {
  if (!request.hasContext()) {
    request.setContext(std::make_unique<Context>());
  }

  const auto& request_context = request.context();
  auto context = std::make_unique<Context>();

  // Set context.
  if (status != ResponseStatus::Ok) {
    context->setMessageType(MessageType::Exception);
  } else if (type.has_value() &&
             (type.value() == RpcResponseType::ResponseWithException ||
              type.value() == RpcResponseType::ResponseWithExceptionWithAttachments)) {
    context->setMessageType(MessageType::Exception);
  } else {
    context->setMessageType(MessageType::Response);
  }

  context->setResponseStatus(status);
  context->setRequestId(request_context.requestId());

  // Set response.
  auto response = std::make_unique<RpcResponse>();
  if (status == ResponseStatus::Ok) {
    // No response type for non-Ok response.
    response->setResponseType(type.value_or(RpcResponseType::ResponseWithValue));
  }
  response->content().initialize(std::make_unique<Hessian2::StringObject>(content), {});

  auto metadata = std::make_shared<MessageMetadata>();
  metadata->setContext(std::move(context));
  metadata->setResponse(std::move(response));

  return metadata;
}

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
