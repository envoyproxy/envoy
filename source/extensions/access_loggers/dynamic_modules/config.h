#pragma once

#include "envoy/access_log/access_log_config.h"

#include "source/extensions/access_loggers/dynamic_modules/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

class DynamicModuleAccessLogFactory : public AccessLog::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::GenericFactoryContext& context,
                          std::vector<Formatter::CommandParserPtr>&& command_parsers) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.access_loggers.dynamic_modules"; }
};

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
