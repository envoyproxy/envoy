#pragma once

#include "envoy/server/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

/**
 * Config registration for the standard error access log. @see AccessLogInstanceFactory.
 */
class StderrorAccessLogFactory : public Server::Configuration::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::CommonFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy