#include "extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/macros.h"

#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "extensions/filters/network/dubbo_proxy/message_impl.h"

#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

std::pair<RpcInvocationSharedPtr, bool>
DubboHessian2SerializerImpl::deserializeRpcInvocation(Buffer::Instance& buffer,
                                                      ContextSharedPtr context) {
  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));

  // TODO(zyfjeff): Add format checker
  auto dubbo_version = decoder.decode<std::string>();
  auto service_name = decoder.decode<std::string>();
  auto service_version = decoder.decode<std::string>();
  auto method_name = decoder.decode<std::string>();

  size_t total_size = decoder.offset();
  if (static_cast<uint64_t>(context->bodySize()) < decoder.offset()) {
    throw EnvoyException(fmt::format("RpcInvocation size({}) large than body size({})", total_size,
                                     context->bodySize()));
  }

  if (!dubbo_version || !service_name || !service_version || !method_name) {
    throw EnvoyException(fmt::format("RpcInvocation has no request metadata"));
  }

  auto invo = std::make_shared<RpcInvocationImpl>();
  invo->setServiceName(*service_name);
  invo->setServiceVersion(*service_version);
  invo->setMethodName(*method_name);

  size_t parsed_size = context->headerSize() + decoder.offset();
  size_t buffer_size = context->headerSize() + context->bodySize();

  auto delayed_decoder = std::make_shared<Hessian2::Decoder>(
      std::make_unique<BufferReader>(context->originMessage(), parsed_size));

  invo->setParametersLazyCallback([delayed_decoder](RpcInvocationImpl::ParametersPtr& params) {
    params = std::make_unique<RpcInvocationImpl::Parameters>();

    if (auto types = delayed_decoder->decode<std::string>(); types && !types->empty()) {
      size_t number = HessianUtils::getParametersNumber(*types);
      for (uint32_t i = 0; i < number; i++) {
        if (auto result = delayed_decoder->decode<Hessian2::Object>(); result) {
          params->push_back(std::move(result));
        } else {
          throw EnvoyException("Cannot parse RpcInvocation parameter from buffer");
        }
      }
    }
  });

  invo->setAttachmentLazyCallback(
      [delayed_decoder, buffer_size](RpcInvocationImpl::AttachmentPtr& attach) {
        if (delayed_decoder->offset() >= buffer_size) {
          // No more data for attachment.
          attach = std::make_unique<RpcInvocationImpl::Attachment>(
              std::make_unique<RpcInvocationImpl::Attachment::MapObject>());
          return;
        }

        auto result = delayed_decoder->decode<Hessian2::Object>();
        if (result && result->type() == Hessian2::Object::Type::UntypedMap) {
          attach = std::make_unique<RpcInvocationImpl::Attachment>(
              RpcInvocationImpl::Attachment::MapObjectPtr{
                  dynamic_cast<RpcInvocationImpl::Attachment::MapObject*>(result.release())});
        } else {
          attach = std::make_unique<RpcInvocationImpl::Attachment>(
              std::make_unique<RpcInvocationImpl::Attachment::MapObject>());
        }
      });

  return std::pair<RpcInvocationSharedPtr, bool>(invo, true);
}

std::pair<RpcResultSharedPtr, bool>
DubboHessian2SerializerImpl::deserializeRpcResult(Buffer::Instance& buffer,
                                                  ContextSharedPtr context) {
  ASSERT(buffer.length() >= context->bodySize());
  bool has_value = true;

  auto result = std::make_shared<RpcResultImpl>();

  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));
  auto type_value = decoder.decode<int32_t>();
  if (!type_value) {
    throw EnvoyException(fmt::format("Cannot parse RpcResult type from buffer"));
  }

  RpcResponseType type = static_cast<RpcResponseType>(*type_value);

  switch (type) {
  case RpcResponseType::ResponseWithException:
  case RpcResponseType::ResponseWithExceptionWithAttachments:
    result->setException(true);
    break;
  case RpcResponseType::ResponseWithNullValue:
  case RpcResponseType::ResponseNullValueWithAttachments:
    has_value = false;
    FALLTHRU;
  case RpcResponseType::ResponseWithValue:
  case RpcResponseType::ResponseValueWithAttachments:
    result->setException(false);
    break;
  default:
    throw EnvoyException(fmt::format("not supported return type {}", static_cast<uint8_t>(type)));
  }

  size_t total_size = decoder.offset();

  if (context->bodySize() < total_size) {
    throw EnvoyException(fmt::format("RpcResult size({}) large than body size({})", total_size,
                                     context->bodySize()));
  }

  if (!has_value && context->bodySize() != total_size) {
    throw EnvoyException(
        fmt::format("RpcResult is no value, but the rest of the body size({}) not equal 0",
                    (context->bodySize() - total_size)));
  }

  return std::pair<RpcResultSharedPtr, bool>(result, true);
}

size_t DubboHessian2SerializerImpl::serializeRpcResult(Buffer::Instance& output_buffer,
                                                       const std::string& content,
                                                       RpcResponseType type) {
  size_t origin_length = output_buffer.length();
  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(output_buffer));

  // The serialized response type is compact int.
  bool result = encoder.encode(static_cast<std::underlying_type<RpcResponseType>::type>(type));
  result |= encoder.encode(content);

  ASSERT(result);

  return output_buffer.length() - origin_length;
}

class DubboHessian2SerializerConfigFactory
    : public SerializerFactoryBase<DubboHessian2SerializerImpl> {
public:
  DubboHessian2SerializerConfigFactory()
      : SerializerFactoryBase(ProtocolType::Dubbo, SerializationType::Hessian2) {}
};

/**
 * Static registration for the Hessian protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(DubboHessian2SerializerConfigFactory, NamedSerializerConfigFactory);

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
