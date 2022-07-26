#pragma once

#include "envoy/common/exception.h"

#include "contrib/sip_proxy/filters/network/source/metadata.h"
#include "contrib/sip_proxy/filters/network/source/sip.h"
#include "contrib/sip_proxy/filters/network/source/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

struct AppException : public EnvoyException,
                      public DirectResponse,
                      Logger::Loggable<Logger::Id::connection> {
  AppException(AppExceptionType type, ErrorCode error_code, const std::string& what)
      : EnvoyException(what), type_(type), error_code_(error_code) {}
  AppException(AppExceptionType type, const std::string& what)
      : EnvoyException(what), type_(type) {}
  AppException(const AppException& ex) : EnvoyException(ex.what()), type_(ex.type_) {}

  ResponseType encode(MessageMetadata& metadata, Buffer::Instance& buffer) const override;

  const AppExceptionType type_;

  const ErrorCode error_code_ {ErrorCode::service_unavailable};

};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
