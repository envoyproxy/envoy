#pragma once

#include "envoy/common/exception.h"

#include "extensions/filters/network/thrift_proxy/metadata.h"
#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

struct AppException : public EnvoyException, public DirectResponse {
  AppException(AppExceptionType type, const std::string& what)
      : EnvoyException(what), type_(type) {}
  AppException(const AppException& ex) : EnvoyException(ex.what()), type_(ex.type_) {}

  void encode(MessageMetadata& metadata, Protocol& proto, Buffer::Instance& buffer) const override;

  const AppExceptionType type_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
