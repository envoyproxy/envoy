#include "extensions/filters/network/dubbo_proxy/app_exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/dubbo_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

AppException::AppException(ResponseStatus status, const std::string& what)
    : EnvoyException(what), status_(status),
      response_type_(RpcResponseType::ResponseWithException) {}

AppException::ResponseType AppException::encode(MessageMetadata& metadata,
                                                DubboProxy::Protocol& protocol,
                                                Deserializer& deserializer,
                                                Buffer::Instance& buffer) const {
  ASSERT(buffer.length() == 0);

  ENVOY_LOG(debug, "err {}", what());

  // Serialize the response content to get the serialized response length.
  const std::string& response = what();
  size_t serialized_body_size = deserializer.serializeRpcResult(buffer, response, response_type_);

  metadata.setResponseStatus(status_);
  metadata.setMessageType(MessageType::Response);

  Buffer::OwnedImpl protocol_buffer;
  if (!protocol.encode(protocol_buffer, serialized_body_size, metadata)) {
    throw EnvoyException("failed to encode local reply message");
  }

  buffer.prepend(protocol_buffer);

  return DirectResponse::ResponseType::Exception;
}

DownstreamConnectionCloseException::DownstreamConnectionCloseException(const std::string& what)
    : EnvoyException(what) {}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
