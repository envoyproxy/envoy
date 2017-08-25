#pragma once

#include <string>

#include "envoy/http/access_log.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the file access log. @see AccessLogInstanceFactory.
 */
class FileAccessLogFactory : public Http::AccessLog::AccessLogInstanceFactory {
public:
  Http::AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, Http::AccessLog::FilterPtr&& filter,
                          AccessLog::AccessLogManager& log_manager) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy