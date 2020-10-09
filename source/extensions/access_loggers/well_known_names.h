#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {

/**
 * Well-known access logger names.
 * NOTE: New access loggers should use the well known name: envoy.access_loggers.name.
 */
class AccessLogNameValues {
public:
  // File access log
  const std::string File = "envoy.access_loggers.file";
  // HTTP gRPC access log
  const std::string HttpGrpc = "envoy.access_loggers.http_grpc";
  // TCP gRPC access log
  const std::string TcpGrpc = "envoy.access_loggers.tcp_grpc";
  // WASM access log
  const std::string Wasm = "envoy.access_loggers.wasm";
};

using AccessLogNames = ConstSingleton<AccessLogNameValues>;

} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
