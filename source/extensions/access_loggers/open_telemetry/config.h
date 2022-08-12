#pragma once

#include <string>

#include "envoy/server/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

/**
 * Config registration for the OpenTelemetry (gRPC) access log. @see AccessLogInstanceFactory.
 */
class AccessLogFactory : public Server::Configuration::AccessLogInstanceFactory {
public:
  ::Envoy::AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, ::Envoy::AccessLog::FilterPtr&& filter,
                          Server::Configuration::ListenerAccessLogFactoryContext& context) override;

  ::Envoy::AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, ::Envoy::AccessLog::FilterPtr&& filter,
                          Server::Configuration::CommonFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
