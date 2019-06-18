#pragma once

#include "envoy/common/exception.h"

#include "extensions/filters/network/dubbo_proxy/deserializer.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

struct AppException : public EnvoyException,
                      public DubboFilters::DirectResponse,
                      Logger::Loggable<Logger::Id::dubbo> {
  AppException(ResponseStatus status, const std::string& what);
  AppException(const AppException& ex) = default;

  using ResponseType = DubboFilters::DirectResponse::ResponseType;
  ResponseType encode(MessageMetadata& metadata, Protocol& protocol, Deserializer& deserializer,
                      Buffer::Instance& buffer) const override;

  const ResponseStatus status_;
  const RpcResponseType response_type_;
};

struct DownstreamConnectionCloseException : public EnvoyException {
  DownstreamConnectionCloseException(const std::string& what);
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
