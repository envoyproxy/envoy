#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"

#include "envoy/registry/registry.h"

#include "common/common/assert.h"

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
  case SerializationType::Hessian:
  case SerializationType::Json:
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
  case ResponseStatus::ClientError:
  case ResponseStatus::ServerThreadpoolExhaustedError:
    break;
  default:
    return false;
  }
  return true;
}

void parseRequestInfoFromBuffer(Buffer::Instance& data, MessageMetadataSharedPtr metadata) {
  ASSERT(data.length() >= DubboProtocolImpl::MessageSize);
  uint8_t flag = data.peekInt<uint8_t>(FlagOffset);
  bool is_two_way = (flag & TwoWayMask) == TwoWayMask ? true : false;
  SerializationType type = static_cast<SerializationType>(flag & SerializationTypeMask);
  if (!isValidSerializationType(type)) {
    throw EnvoyException(
        fmt::format("invalid dubbo message serialization type {}",
                    static_cast<std::underlying_type<SerializationType>::type>(type)));
  }

  if (!is_two_way) {
    metadata->setMessageType(MessageType::Oneway);
  }

  metadata->setSerializationType(type);
}

void parseResponseInfoFromBuffer(Buffer::Instance& buffer, MessageMetadataSharedPtr metadata) {
  ASSERT(buffer.length() >= DubboProtocolImpl::MessageSize);
  ResponseStatus status = static_cast<ResponseStatus>(buffer.peekInt<uint8_t>(StatusOffset));
  if (!isValidResponseStatus(status)) {
    throw EnvoyException(
        fmt::format("invalid dubbo message response status {}",
                    static_cast<std::underlying_type<ResponseStatus>::type>(status)));
  }

  metadata->setResponseStatus(status);
}

bool DubboProtocolImpl::decode(Buffer::Instance& buffer, Protocol::Context* context,
                               MessageMetadataSharedPtr metadata) {
  if (!metadata) {
    throw EnvoyException("invalid metadata parameter");
  }

  if (buffer.length() < DubboProtocolImpl::MessageSize) {
    return false;
  }

  uint16_t magic_number = buffer.peekBEInt<uint16_t>();
  if (magic_number != MagicNumber) {
    throw EnvoyException(fmt::format("invalid dubbo message magic number {}", magic_number));
  }

  uint8_t flag = buffer.peekInt<uint8_t>(FlagOffset);
  MessageType type =
      (flag & MessageTypeMask) == MessageTypeMask ? MessageType::Request : MessageType::Response;
  bool is_event = (flag & EventMask) == EventMask ? true : false;
  int64_t request_id = buffer.peekBEInt<int64_t>(RequestIDOffset);
  int32_t body_size = buffer.peekBEInt<int32_t>(BodySizeOffset);

  // The body size of the heartbeat message is zero.
  if (body_size > MaxBodySize || body_size < 0) {
    throw EnvoyException(fmt::format("invalid dubbo message size {}", body_size));
  }

  metadata->setMessageType(type);
  metadata->setRequestId(request_id);

  if (type == MessageType::Request) {
    parseRequestInfoFromBuffer(buffer, metadata);
  } else {
    parseResponseInfoFromBuffer(buffer, metadata);
  }

  context->header_size_ = DubboProtocolImpl::MessageSize;
  context->body_size_ = body_size;
  context->is_heartbeat_ = is_event;

  return true;
}

bool DubboProtocolImpl::encode(Buffer::Instance& buffer, int32_t body_size,
                               const MessageMetadata& metadata) {
  switch (metadata.message_type()) {
  case MessageType::Response: {
    ASSERT(metadata.response_status().has_value());
    buffer.writeBEInt<uint16_t>(MagicNumber);
    uint8_t flag = static_cast<uint8_t>(metadata.serialization_type());
    if (metadata.is_event()) {
      ASSERT(0 == body_size);
      flag = flag ^ EventMask;
    }
    buffer.writeByte(flag);
    buffer.writeByte(static_cast<uint8_t>(metadata.response_status().value()));
    buffer.writeBEInt<uint64_t>(metadata.request_id());
    buffer.writeBEInt<uint32_t>(body_size);
    return true;
  }
  case MessageType::Request: {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
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
