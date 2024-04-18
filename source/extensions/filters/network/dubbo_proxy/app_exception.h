#pragma once

#include "envoy/common/exception.h"

#include "source/common/common/utility.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "source/extensions/filters/network/dubbo_proxy/metadata.h"
#include "source/extensions/filters/network/dubbo_proxy/protocol.h"
#include "source/extensions/filters/network/dubbo_proxy/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using ResponseType = DubboFilters::DirectResponse::ResponseType;

template <typename T = ResponseStatus>
struct AppExceptionBase : public EnvoyException,
                          public DubboFilters::DirectResponse,
                          Logger::Loggable<Logger::Id::dubbo> {
  AppExceptionBase(const AppExceptionBase& ex) = default;
  AppExceptionBase(T status, const std::string& what) : EnvoyException(what), status_(status) {}

  ResponseType encode(MessageMetadata& metadata, DubboProxy::Protocol& protocol,
                      Buffer::Instance& buffer) const override {
    ASSERT(buffer.length() == 0);

    ENVOY_LOG(debug, "Exception information: {}", what());

    metadata.setResponseStatus<T>(status_);
    metadata.setMessageType(MessageType::Response);
    if (!protocol.encode(buffer, metadata, what(), response_type_)) {
      ExceptionUtil::throwEnvoyException("Failed to encode local reply message");
    }

    return ResponseType::Exception;
  }

  const T status_;
  const RpcResponseType response_type_{RpcResponseType::ResponseWithException};
};

using AppException = AppExceptionBase<>;

struct DownstreamConnectionCloseException : public EnvoyException {
  DownstreamConnectionCloseException(const std::string& what);
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
