#include "source/extensions/common/dubbo/hessian2_serializer_impl.h"

#include <cstddef>

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/extensions/common/dubbo/hessian2_utils.h"
#include "source/extensions/common/dubbo/message_impl.h"

#include "hessian2/object.hpp"
#include "message.h"
#include "metadata.h"

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
  auto request = std::make_unique<RpcRequestImpl>();

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));

  // TODO(zyfjeff): Add format checker
  auto dubbo_version = decoder.decode<std::string>();
  auto service_name = decoder.decode<std::string>();
  auto service_version = decoder.decode<std::string>();
  auto method_name = decoder.decode<std::string>();

  if (context.bodySize() < decoder.offset()) {
    throw EnvoyException(fmt::format("RpcRequest size({}) larger than body size({})",
                                     decoder.offset(), context.bodySize()));
  }

  if (dubbo_version == nullptr || service_name == nullptr || service_version == nullptr ||
      method_name == nullptr) {
    throw EnvoyException(fmt::format("RpcRequest has no request metadata"));
  }

  request->setServiceName(*service_name);
  request->setServiceVersion(*service_version);
  request->setMethodName(*method_name);

  // Move original request message to the raw buffer to delay the decoding of complex body.
  request->messageBuffer().move(buffer, context.bodySize());
  const size_t parsed_size = decoder.offset();
  auto delayed_decoder = std::make_shared<Hessian2::Decoder>(
      std::make_unique<BufferReader>(request->messageBuffer(), parsed_size));

  request->setParametersLazyCallback([delayed_decoder]() -> RpcRequestImpl::ParametersPtr {
    auto params = std::make_unique<RpcRequestImpl::Parameters>();

    if (auto types = delayed_decoder->decode<std::string>(); types != nullptr && !types->empty()) {
      uint32_t number = Hessian2Utils::getParametersNumber(*types);
      for (uint32_t i = 0; i < number; i++) {
        if (auto result = delayed_decoder->decode<Hessian2::Object>(); result != nullptr) {
          params->push_back(std::move(result));
        } else {
          throw EnvoyException("Cannot parse RpcRequest parameter from buffer");
        }
      }
    }
    return params;
  });

  request->setAttachmentLazyCallback([delayed_decoder]() -> RpcRequestImpl::AttachmentPtr {
    size_t offset = delayed_decoder->offset();

    auto result = delayed_decoder->decode<Hessian2::Object>();
    if (result != nullptr && result->type() == Hessian2::Object::Type::UntypedMap) {
      return std::make_unique<RpcRequestImpl::Attachment>(
          RpcRequestImpl::Attachment::MapPtr{
              dynamic_cast<RpcRequestImpl::Attachment::Map*>(result.release())},
          offset);
    } else {
      return std::make_unique<RpcRequestImpl::Attachment>(
          std::make_unique<RpcRequestImpl::Attachment::Map>(), offset);
    }
  });

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

  // Handle normal response or exception response.
  ASSERT(context.messageType() == MessageType::Response ||
         context.messageType() == MessageType::Exception);
  auto response = std::make_unique<RpcResponseImpl>();

  // Non `Ok` response body has no response type info and skip deserialization.
  if (context.messageType() == MessageType::Exception) {
    ASSERT(context.hasResponseStatus());
    ASSERT(context.responseStatus() != ResponseStatus::Ok);
    response->messageBuffer().move(buffer, context.bodySize());
    return response;
  }

  bool has_value = true;

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));
  auto type_value = decoder.decode<int32_t>();
  if (type_value == nullptr) {
    throw EnvoyException(fmt::format("Cannot parse RpcResponse type from buffer"));
  }

  RpcResponseType type = static_cast<RpcResponseType>(*type_value);
  response->setResponseType(type);

  switch (type) {
  case RpcResponseType::ResponseWithException:
  case RpcResponseType::ResponseWithExceptionWithAttachments:
    context.setMessageType(MessageType::Exception);
    break;
  case RpcResponseType::ResponseWithNullValue:
    has_value = false;
    FALLTHRU;
  case RpcResponseType::ResponseNullValueWithAttachments:
  case RpcResponseType::ResponseWithValue:
  case RpcResponseType::ResponseValueWithAttachments:
    break;
  default:
    throw EnvoyException(fmt::format("not supported return type {}", static_cast<uint8_t>(type)));
  }

  size_t total_size = decoder.offset();

  if (context.bodySize() < total_size) {
    throw EnvoyException(fmt::format("RpcResponse size({}) large than body size({})", total_size,
                                     context.bodySize()));
  }

  if (!has_value && context.bodySize() != total_size) {
    throw EnvoyException(
        fmt::format("RpcResponse is no value, but the rest of the body size({}) not equal 0",
                    (context.bodySize() - total_size)));
  }

  response->messageBuffer().move(buffer, context.bodySize());
  return response;
}

void Hessian2SerializerImpl::serializeRpcResponse(Buffer::Instance& buffer,
                                                  MessageMetadata& metadata) {
  ASSERT(metadata.hasContext());
  const auto& context = metadata.context();

  if (context.heartbeat()) {
    buffer.writeByte('N');
    return;
  }

  ASSERT(dynamic_cast<RpcResponseImpl*>(&metadata.mutableResponse()) != nullptr);
  auto* typed_response_info = dynamic_cast<RpcResponseImpl*>(&metadata.mutableResponse());

  if (typed_response_info->localRawMessage().has_value()) {
    // Local direct response.
    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

    if (typed_response_info->responseType().has_value()) {
      encoder.encode(static_cast<int32_t>(typed_response_info->responseType().value()));
    }
    encoder.encode<absl::string_view>(typed_response_info->localRawMessage().value());
  } else {
    // Update of dubbo response is not supported for now. And there is no retry for response
    // encoding. So, it is safe to move the data directly.
    buffer.move(typed_response_info->messageBuffer());
  }
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

  ASSERT(dynamic_cast<RpcRequestImpl*>(&metadata.mutableRequest()) != nullptr);
  auto* typed_request_info = dynamic_cast<RpcRequestImpl*>(&metadata.mutableRequest());

  // Create a copy for possible retry.
  Buffer::OwnedImpl copy_of_message = typed_request_info->messageBuffer();

  if (typed_request_info->hasAttachment() && typed_request_info->attachment().attachmentUpdated()) {
    const size_t attachment_offset = typed_request_info->attachment().attachmentOffset();
    ASSERT(attachment_offset <= copy_of_message.length());

    // Move the parameters to buffer.
    buffer.move(copy_of_message, attachment_offset);

    // Re-encode the dubbo attachment.
    Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));
    encoder.encode(typed_request_info->attachment().attachment());
  } else {
    buffer.move(copy_of_message);
  }
}

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
