#include "extensions/filters/network/dubbo_proxy/app_exception.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"
#include "common/common/hex.h"

#include "extensions/filters/network/dubbo_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

AppException::AppException(AppExceptionType type, const std::string& what)
    : EnvoyException(what), type_(type), response_type_(RpcResponseType::ResponseWithException) {}

AppException::AppException(const AppException& ex) : EnvoyException(ex.what()), type_(ex.type_) {}

AppException::ResponseType AppException::encode(MessageMetadata& metadata,
                                                DubboProxy::Protocol& protocol,
                                                Deserializer& deserializer,
                                                Buffer::Instance& buffer) const {
  ENVOY_LOG(debug, "err {}", what());

  switch (type_) {
  case AppExceptionType::ClientTimeout:
    metadata.setResponseStatus(ResponseStatus::ClientTimeout);
    break;
  case AppExceptionType::ServerTimeout:
    metadata.setResponseStatus(ResponseStatus::ServerTimeout);
    break;
  case AppExceptionType::BadRequest:
    metadata.setResponseStatus(ResponseStatus::BadRequest);
    break;
  case AppExceptionType::BadResponse:
    metadata.setResponseStatus(ResponseStatus::BadResponse);
    break;
  case AppExceptionType::ServiceNotFound:
    metadata.setResponseStatus(ResponseStatus::ServiceNotFound);
    break;
  case AppExceptionType::ServiceError:
    metadata.setResponseStatus(ResponseStatus::ServiceError);
    break;
  case AppExceptionType::ServerError:
    metadata.setResponseStatus(ResponseStatus::ServerError);
    break;
  case AppExceptionType::ClientError:
    metadata.setResponseStatus(ResponseStatus::ClientError);
    break;
  case AppExceptionType::ServerThreadpoolExhaustedError:
    metadata.setResponseStatus(ResponseStatus::ServerThreadpoolExhaustedError);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE
  }

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
