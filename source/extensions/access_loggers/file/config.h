#pragma once

#include "envoy/access_log/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

/**
 * Config registration for the file access log. @see AccessLogInstanceFactory.
 */
class FileAccessLogFactory : public AccessLog::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
