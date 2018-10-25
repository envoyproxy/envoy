#pragma once

#include "envoy/server/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

/**
 * Config registration for the file access log. @see AccessLogInstanceFactory.
 */
class FileAccessLogFactory : public Server::Configuration::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;

private:
  std::unordered_map<std::string, std::string> convertJsonFormatToMap(ProtobufWkt::Struct config);
};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
