#include "source/extensions/common/dubbo/hessian2_serializer_impl.h"

#include <cstddef>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/extensions/common/dubbo/hessian2_utils.h"
#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/metadata.h"

#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

RpcRequestPtr Hessian2SerializerImpl::deserializeRpcRequest(Buffer::Instance& buffer,
                                                            Context& context) {
  ASSERT(context.bodySize() <= buffer.length());

  // Handle heartbeat.
  if (context.heartbeat()) {
    buffer.drain(context.bodySize());
    return nullptr;
  }

  // Handle normal request or oneway request.
  ASSERT(context.messageType() == MessageType::Request ||
         context.messageType() == MessageType::Oneway);
  ASSERT(context.bodySize() <= buffer.length());

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));

  // TODO(zyfjeff): Add format checker
  auto dubbo_version = decoder.decode<std::string>();
  auto service_name = decoder.decode<std::string>();
  auto service_version = decoder.decode<std::string>();
  auto method_name = decoder.decode<std::string>();

  const auto decoded_size = decoder.offset();

  if (context.bodySize() < decoded_size) {
    throw EnvoyException(fmt::format("RpcRequest size({}) larger than body size({})", decoded_size,
                                     context.bodySize()));
  }

  if (dubbo_version == nullptr || service_name == nullptr || service_version == nullptr ||
      method_name == nullptr) {
    throw EnvoyException(fmt::format("RpcRequest has no request metadata"));
  }

  buffer.drain(decoded_size);

  auto request = std::make_unique<RpcRequest>(std::move(*dubbo_version), std::move(*service_name),
                                              std::move(*service_version), std::move(*method_name));
  request->content().initialize(buffer, context.bodySize() - decoded_size);

  return request;
}

RpcResponsePtr Hessian2SerializerImpl::deserializeRpcResponse(Buffer::Instance& buffer,
                                                              Context& context) {
  ASSERT(context.bodySize() <= buffer.length());

  // Handle heartbeat.
  if (context.heartbeat()) {
    buffer.drain(context.bodySize());
    return nullptr;
  }

  ASSERT(context.hasResponseStatus());

  // Handle normal response or exception response.
  ASSERT(context.messageType() == MessageType::Response ||
         context.messageType() == MessageType::Exception);

  ASSERT(context.hasResponseStatus());
  auto response = std::make_unique<RpcResponse>();

  // Non `Ok` response body has no response type info and skip deserialization.
  if (context.messageType() == MessageType::Exception) {
    ASSERT(context.responseStatus() != ResponseStatus::Ok);
    response->content().initialize(buffer, context.bodySize());
    return response;
  }

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));
  auto type_value = decoder.decode<int32_t>();
  if (type_value == nullptr) {
    throw EnvoyException(fmt::format("Cannot parse RpcResponse type from buffer"));
  }

  const RpcResponseType type = static_cast<RpcResponseType>(*type_value);

  switch (type) {
  case RpcResponseType::ResponseWithException:
  case RpcResponseType::ResponseWithExceptionWithAttachments:
    context.setMessageType(MessageType::Exception);
    break;
  case RpcResponseType::ResponseWithNullValue:
  case RpcResponseType::ResponseNullValueWithAttachments:
  case RpcResponseType::ResponseWithValue:
  case RpcResponseType::ResponseValueWithAttachments:
    break;
  default:
    throw EnvoyException(fmt::format("not supported return type {}", static_cast<uint8_t>(type)));
  }

  const auto decoded_size = decoder.offset();

  if (context.bodySize() < decoded_size) {
    throw EnvoyException(fmt::format("RpcResponse size({}) large than body size({})", decoded_size,
                                     context.bodySize()));
  }

  buffer.drain(decoded_size);

  response->setResponseType(type);
  response->content().initialize(buffer, context.bodySize() - decoded_size);
  return response;
}

void Hessian2SerializerImpl::serializeRpcResponse(Buffer::Instance& buffer,
                                                  MessageMetadata& metadata) {
  ASSERT(metadata.hasContext());
  ASSERT(metadata.context().hasResponseStatus());

  const auto& context = metadata.context();

  if (context.heartbeat()) {
    buffer.writeByte('N');
    return;
  }

  ASSERT(metadata.hasResponse());
  if (auto type = metadata.response().responseType(); type.has_value()) {
    ASSERT(metadata.context().responseStatus() == ResponseStatus::Ok);
    buffer.writeByte(0x90 + static_cast<uint8_t>(type.value()));
  }

  buffer.add(metadata.response().content().buffer());
}

void Hessian2SerializerImpl::serializeRpcRequest(Buffer::Instance& buffer,
                                                 MessageMetadata& metadata) {

  ASSERT(metadata.hasContext());
  const auto& context = metadata.context();

  if (context.heartbeat()) {
    buffer.writeByte('N');
    return;
  }

  ASSERT(metadata.hasRequest());
  ASSERT(metadata.context().messageType() == MessageType::Request ||
         metadata.context().messageType() == MessageType::Oneway);

  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

  encoder.encode<absl::string_view>(metadata.request().version());
  encoder.encode<absl::string_view>(metadata.request().service());
  encoder.encode<absl::string_view>(metadata.request().serviceVersion());
  encoder.encode<absl::string_view>(metadata.request().method());

  buffer.add(metadata.request().content().buffer());
}

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
