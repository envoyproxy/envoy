#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"

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

void RequestMessageImpl::fromBuffer(Buffer::Instance& data) {
  ASSERT(data.length() >= DubboProtocolImpl::MessageSize);
  uint8_t flag = data.peekInt<uint8_t>(FlagOffset);
  is_two_way_ = (flag & TwoWayMask) == TwoWayMask ? true : false;
  type_ = static_cast<SerializationType>(flag & SerializationTypeMask);
  if (!isValidSerializationType(type_)) {
    throw EnvoyException(
        fmt::format("invalid dubbo message serialization type {}",
                    static_cast<std::underlying_type<SerializationType>::type>(type_)));
  }
}

void ResponseMessageImpl::fromBuffer(Buffer::Instance& buffer) {
  ASSERT(buffer.length() >= DubboProtocolImpl::MessageSize);
  status_ = static_cast<ResponseStatus>(buffer.peekInt<uint8_t>(StatusOffset));
  if (!isValidResponseStatus(status_)) {
    throw EnvoyException(
        fmt::format("invalid dubbo message response status {}",
                    static_cast<std::underlying_type<ResponseStatus>::type>(status_)));
  }
}

bool DubboProtocolImpl::decode(Buffer::Instance& buffer, Protocol::Context* context) {
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

  if (body_size > MaxBodySize || body_size <= 0) {
    throw EnvoyException(fmt::format("invalid dubbo message size {}", body_size));
  }

  context->body_size_ = body_size;

  if (type == MessageType::Request) {
    RequestMessageImplPtr req =
        std::make_unique<RequestMessageImpl>(request_id, body_size, is_event);
    req->fromBuffer(buffer);
    context->is_request_ = true;
    callbacks_.onRequestMessage(std::move(req));
  } else {
    ResponseMessageImplPtr res =
        std::make_unique<ResponseMessageImpl>(request_id, body_size, is_event);
    res->fromBuffer(buffer);
    callbacks_.onResponseMessage(std::move(res));
  }

  buffer.drain(MessageSize);
  return true;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
