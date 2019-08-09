#pragma once

#include <string>

#include "envoy/server/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

/**
 * Config registration for the HTTP gRPC access log. @see AccessLogInstanceFactory.
 */
class HttpGrpcAccessLogFactory : public Server::Configuration::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

// TODO(mattklein123): Add TCP access log and refactor into base/concrete gRPC access logs.

} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
