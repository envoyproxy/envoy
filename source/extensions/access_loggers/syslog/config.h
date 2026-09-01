#pragma once

#include "envoy/access_log/access_log_config.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

absl::Status validateSyslogConfig(
    const envoy::extensions::access_loggers::syslog::v3::SyslogAccessLogConfig& config);

class SyslogAccessLogFactory : public AccessLog::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::GenericFactoryContext& context,
                          std::vector<Formatter::CommandParserPtr>&& command_parsers = {}) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
