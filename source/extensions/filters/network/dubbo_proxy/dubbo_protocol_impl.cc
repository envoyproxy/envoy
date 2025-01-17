#include "source/extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace {

constexpr uint16_t MagicNumber = 0xdabb;
constexpr uint8_t MessageTypeMask = 0x80;
constexpr uint8_t EventMask = 0x20;
constexpr uint8_t TwoWayMask = 0x40;
constexpr uint8_t SerializationTypeMask = 0x1f;
constexpr uint64_t FlagOffset = 2;
constexpr uint64_t StatusOffset = 3;
constexpr uint64_t RequestIDOffset = 4;
constexpr uint64_t BodySizeOffset = 12;

} // namespace

// Consistent with the SerializationType
bool isValidSerializationType(SerializationType type) {
  switch (type) {
  case SerializationType::Hessian2:
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

void parseRequestInfoFromBuffer(Buffer::Instance& data, MessageMetadataSharedPtr metadata) {
  ASSERT(data.length() >= DubboProtocolImpl::MessageSize);
  uint8_t flag = data.peekInt<uint8_t>(FlagOffset);
  bool is_two_way = (flag & TwoWayMask) == TwoWayMask ? true : false;
  SerializationType type = static_cast<SerializationType>(flag & SerializationTypeMask);
  if (!isValidSerializationType(type)) {
    throw EnvoyException(
        absl::StrCat("invalid dubbo message serialization type ",
                     static_cast<std::underlying_type<SerializationType>::type>(type)));
  }

  if (!is_two_way && metadata->messageType() != MessageType::HeartbeatRequest) {
    metadata->setMessageType(MessageType::Oneway);
  }

  metadata->setSerializationType(type);
}

void parseResponseInfoFromBuffer(Buffer::Instance& buffer, MessageMetadataSharedPtr metadata) {
  ASSERT(buffer.length() >= DubboProtocolImpl::MessageSize);
  ResponseStatus status = static_cast<ResponseStatus>(buffer.peekInt<uint8_t>(StatusOffset));
  if (!isValidResponseStatus(status)) {
    throw EnvoyException(
        absl::StrCat("invalid dubbo message response status ",
                     static_cast<std::underlying_type<ResponseStatus>::type>(status)));
  }

  metadata->setResponseStatus(status);
}

std::pair<ContextSharedPtr, bool>
DubboProtocolImpl::decodeHeader(Buffer::Instance& buffer, MessageMetadataSharedPtr metadata) {
  if (!metadata) {
    throw EnvoyException("invalid metadata parameter");
  }

  if (buffer.length() < DubboProtocolImpl::MessageSize) {
    return {nullptr, false};
  }

  uint16_t magic_number = buffer.peekBEInt<uint16_t>();
  if (magic_number != MagicNumber) {
    throw EnvoyException(absl::StrCat("invalid dubbo message magic number ", magic_number));
  }

  uint8_t flag = buffer.peekInt<uint8_t>(FlagOffset);
  MessageType type =
      (flag & MessageTypeMask) == MessageTypeMask ? MessageType::Request : MessageType::Response;
  bool is_event = (flag & EventMask) == EventMask ? true : false;
  int64_t request_id = buffer.peekBEInt<int64_t>(RequestIDOffset);
  int32_t body_size = buffer.peekBEInt<int32_t>(BodySizeOffset);

  // The body size of the heartbeat message is zero.
  if (body_size > MaxBodySize || body_size < 0) {
    throw EnvoyException(absl::StrCat("invalid dubbo message size ", body_size));
  }

  metadata->setRequestId(request_id);

  if (type == MessageType::Request) {
    if (is_event) {
      type = MessageType::HeartbeatRequest;
    }
    metadata->setMessageType(type);
    parseRequestInfoFromBuffer(buffer, metadata);
  } else {
    if (is_event) {
      type = MessageType::HeartbeatResponse;
    }
    metadata->setMessageType(type);
    parseResponseInfoFromBuffer(buffer, metadata);
  }

  auto context = std::make_shared<ContextImpl>();
  context->setHeaderSize(DubboProtocolImpl::MessageSize);
  context->setBodySize(body_size);
  context->setHeartbeat(is_event);

  return {context, true};
}

bool DubboProtocolImpl::decodeData(Buffer::Instance& buffer, ContextSharedPtr context,
                                   MessageMetadataSharedPtr metadata) {
  ASSERT(serializer_);

  if ((buffer.length()) < context->bodySize()) {
    return false;
  }

  switch (metadata->messageType()) {
  case MessageType::Oneway:
  case MessageType::Request: {
    auto ret = serializer_->deserializeRpcInvocation(buffer, context);
    if (!ret.second) {
      return false;
    }
    metadata->setInvocationInfo(ret.first);
    break;
  }
  case MessageType::Response: {
    // Non `Ok` response body has no response type info and skip deserialization.
    if (metadata->responseStatus() != ResponseStatus::Ok) {
      metadata->setMessageType(MessageType::Exception);
      break;
    }
    auto ret = serializer_->deserializeRpcResult(buffer, context);
    if (!ret.second) {
      return false;
    }
    if (ret.first->hasException()) {
      metadata->setMessageType(MessageType::Exception);
    }
    break;
  }
  default:
    PANIC("not handled");
  }

  return true;
}

bool DubboProtocolImpl::encode(Buffer::Instance& buffer, const MessageMetadata& metadata,
                               const std::string& content, RpcResponseType type) {
  ASSERT(serializer_);

  switch (metadata.messageType()) {
  case MessageType::HeartbeatResponse: {
    ASSERT(metadata.hasResponseStatus());
    ASSERT(content.empty());
    buffer.writeBEInt<uint16_t>(MagicNumber);
    uint8_t flag = static_cast<uint8_t>(metadata.serializationType());
    flag = flag ^ EventMask;
    buffer.writeByte(flag);
    buffer.writeByte(static_cast<uint8_t>(metadata.responseStatus()));
    buffer.writeBEInt<uint64_t>(metadata.requestId());
    // Body of heart beat response is null.
    // TODO(wbpcode): Currently we only support the Hessian2 serialization scheme, so here we
    // directly use the 'N' for null object in Hessian2. This coupling should be unnecessary.
    buffer.writeBEInt<uint32_t>(1u);
    buffer.writeByte('N');
    return true;
  }
  case MessageType::Response: {
    ASSERT(metadata.hasResponseStatus());
    ASSERT(!content.empty());
    Buffer::OwnedImpl body_buffer;
    size_t serialized_body_size = serializer_->serializeRpcResult(body_buffer, content, type);

    buffer.writeBEInt<uint16_t>(MagicNumber);
    buffer.writeByte(static_cast<uint8_t>(metadata.serializationType()));
    buffer.writeByte(static_cast<uint8_t>(metadata.responseStatus()));
    buffer.writeBEInt<uint64_t>(metadata.requestId());
    buffer.writeBEInt<uint32_t>(serialized_body_size);

    buffer.move(body_buffer, serialized_body_size);
    return true;
  }
  case MessageType::Request:
  case MessageType::Oneway:
  case MessageType::Exception:
    PANIC("not implemented");
  default:
    PANIC("not implemented");
  }
}

class DubboProtocolConfigFactory : public ProtocolFactoryBase<DubboProtocolImpl> {
public:
  DubboProtocolConfigFactory() : ProtocolFactoryBase(ProtocolType::Dubbo) {}
};

/**
 * Static registration for the Dubbo protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(DubboProtocolConfigFactory, NamedProtocolConfigFactory);

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
