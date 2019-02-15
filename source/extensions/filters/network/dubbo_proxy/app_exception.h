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

/**
 * Dubbo Application Exception types.
 */
enum class AppExceptionType {
  ClientTimeout,
  ServerTimeout,
  BadRequest,
  BadResponse,
  ServiceNotFound,
  ServiceError,
  ServerError,
  ClientError,
  ServerThreadpoolExhaustedError,
};

struct AppException : public EnvoyException,
                      public DubboFilters::DirectResponse,
                      Logger::Loggable<Logger::Id::dubbo> {
  AppException(AppExceptionType type, const std::string& what);
  AppException(const AppException& ex);

  using ResponseType = DubboFilters::DirectResponse::ResponseType;
  ResponseType encode(MessageMetadata& metadata, Protocol& protocol, Deserializer& deserializer,
                      Buffer::Instance& buffer) const override;

  const AppExceptionType type_;
  RpcResponseType response_type_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
