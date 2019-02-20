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

AppException::AppException(const AppException& ex)
    : EnvoyException(ex.what()), status_(ex.status_) {}

AppException::ResponseType AppException::encode(MessageMetadata& metadata,
                                                DubboProxy::Protocol& protocol,
                                                Deserializer& deserializer,
                                                Buffer::Instance& buffer) const {
  ENVOY_LOG(debug, "err {}", what());

  metadata.setResponseStatus(status_);
  metadata.setMessageType(MessageType::Response);

  const std::string& response = what();
  if (!protocol.encode(buffer, response.size(), metadata)) {
    throw EnvoyException("failed to encode local reply message");
  }

  deserializer.serializeRpcResult(buffer, response, response_type_);

  return DirectResponse::ResponseType::Exception;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
