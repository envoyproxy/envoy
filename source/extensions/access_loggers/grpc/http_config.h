#pragma once

#include <string>

#include "envoy/access_log/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

/**
 * Config registration for the HTTP gRPC access log. @see AccessLogInstanceFactory.
 */
class HttpGrpcAccessLogFactory : public AccessLog::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
