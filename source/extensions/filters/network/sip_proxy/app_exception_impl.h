#pragma once

#include "envoy/common/exception.h"

#include "extensions/filters/network/sip_proxy/metadata.h"
#include "extensions/filters/network/sip_proxy/protocol.h"
#include "extensions/filters/network/sip_proxy/sip.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

struct AppException : public EnvoyException, public DirectResponse {
  AppException(AppExceptionType type, const std::string& what)
      : EnvoyException(what), type_(type) {}
  AppException(const AppException& ex) : EnvoyException(ex.what()), type_(ex.type_) {}

  ResponseType encode(MessageMetadata& metadata, Buffer::Instance& buffer) const override;

  const AppExceptionType type_;
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
