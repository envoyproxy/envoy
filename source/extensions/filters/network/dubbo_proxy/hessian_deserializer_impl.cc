#include "extensions/filters/network/dubbo_proxy/hessian_deserializer_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/macros.h"

#include "extensions/filters/network/dubbo_proxy/deserializer.h"
#include "extensions/filters/network/dubbo_proxy/deserializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

void HessianDeserializerImpl::deserializeRpcInvocation(Buffer::Instance& buffer, size_t body_size,
                                                       MessageMetadataSharedPtr metadata) {
  ASSERT(buffer.length() >= static_cast<uint64_t>(body_size));
  size_t total_size = 0, size;
  // TODO(zyfjeff): Add format checker
  std::string dubbo_version = HessianUtils::peekString(buffer, &size);
  total_size = total_size + size;
  std::string service_name = HessianUtils::peekString(buffer, &size, total_size);
  total_size = total_size + size;
  std::string service_version = HessianUtils::peekString(buffer, &size, total_size);
  total_size = total_size + size;
  std::string method_name = HessianUtils::peekString(buffer, &size, total_size);
  total_size = total_size + size;

  if (static_cast<uint64_t>(body_size) < total_size) {
    throw EnvoyException(
        fmt::format("RpcInvocation size({}) large than body size({})", total_size, body_size));
  }

  metadata->setServiceName(service_name);
  metadata->setServiceVersion(service_version);
  metadata->setMethodName(method_name);
}

RpcResultPtr HessianDeserializerImpl::deserializeRpcResult(Buffer::Instance& buffer,
                                                           size_t body_size) {
  ASSERT(buffer.length() >= body_size);
  size_t total_size = 0;
  bool has_value = true;

  RpcResultPtr result;
  RpcResponseType type = static_cast<RpcResponseType>(HessianUtils::peekInt(buffer, &total_size));

  switch (type) {
  case RpcResponseType::ResponseWithException:
  case RpcResponseType::ResponseWithExceptionWithAttachments:
  case RpcResponseType::ResponseWithValue:
    result = std::make_unique<RpcResultImpl>(true);
    break;
  case RpcResponseType::ResponseWithNullValue:
    has_value = false;
    FALLTHRU;
  case RpcResponseType::ResponseValueWithAttachments:
  case RpcResponseType::ResponseNullValueWithAttachments:
    result = std::make_unique<RpcResultImpl>();
    break;
  default:
    throw EnvoyException(fmt::format("not supported return type {}", static_cast<uint8_t>(type)));
  }

  if (body_size < total_size) {
    throw EnvoyException(
        fmt::format("RpcResult size({}) large than body size({})", total_size, body_size));
  }

  if (!has_value && body_size != total_size) {
    throw EnvoyException(
        fmt::format("RpcResult is no value, but the rest of the body size({}) not equal 0",
                    (body_size - total_size)));
  }

  return result;
}

size_t HessianDeserializerImpl::serializeRpcResult(Buffer::Instance& output_buffer,
                                                   const std::string& content,
                                                   RpcResponseType type) {
  size_t origin_length = output_buffer.length();

  // The serialized response type is compact int.
  size_t serialized_size = HessianUtils::writeInt(
      output_buffer, static_cast<std::underlying_type<RpcResponseType>::type>(type));

  // Serialized response content.
  serialized_size += HessianUtils::writeString(output_buffer, content);

  ASSERT((output_buffer.length() - origin_length) == serialized_size);

  return serialized_size;
}

class HessianDeserializerConfigFactory : public DeserializerFactoryBase<HessianDeserializerImpl> {
public:
  HessianDeserializerConfigFactory() : DeserializerFactoryBase(SerializationType::Hessian) {}
};

/**
 * Static registration for the Hessian protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(HessianDeserializerConfigFactory, NamedDeserializerConfigFactory);

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
