#pragma once

#include <string>

#include "envoy/access_log/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

/**
 * Config registration for the OpenTelemetry (gRPC) access log. @see AccessLogInstanceFactory.
 */
class AccessLogFactory : public Envoy::AccessLog::AccessLogInstanceFactory {
public:
  ::Envoy::AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, ::Envoy::AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
