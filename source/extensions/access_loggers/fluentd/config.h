#pragma once

#include "envoy/access_log/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {

/**
 * Config registration for the fluentd access log. @see AccessLogInstanceFactory.
 */
class FluentdAccessLogFactory : public AccessLog::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
