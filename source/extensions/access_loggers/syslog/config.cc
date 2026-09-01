#include "source/extensions/access_loggers/syslog/config.h"

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_format_utility.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/syslog/syslog_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

absl::Status validateSyslogConfig(const SyslogAccessLogConfig& config) {
  if (config.has_pipe()) {
#ifdef WIN32
    return absl::InvalidArgumentError("syslog Unix domain sockets are not supported on Windows");
#else
    if (config.pipe().path().empty()) {
      return absl::InvalidArgumentError("syslog Unix domain socket path must not be empty");
    }
#endif
  }

  if (!config.no_hostname()) {
    const auto hostname = Formatter::SubstitutionFormatUtils::getHostname();
    if (!hostname.has_value() || hostname->empty()) {
      return absl::InvalidArgumentError(
          "syslog local hostname is unavailable; set no_hostname to true to omit it");
    }
  }

  return absl::OkStatus();
}

AccessLog::InstanceSharedPtr SyslogAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::GenericFactoryContext& context,
    std::vector<Formatter::CommandParserPtr>&& command_parsers) {
  const auto& proto_config = MessageUtil::downcastAndValidate<const SyslogAccessLogConfig&>(
      config, context.messageValidationVisitor());
  THROW_IF_NOT_OK(validateSyslogConfig(proto_config));
  auto formatter =
      THROW_OR_RETURN_VALUE(Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
                                proto_config.log_format(), context, std::move(command_parsers)),
                            Formatter::FormatterPtr);
  Network::Address::InstanceConstSharedPtr destination;
  auto& server_context = context.serverFactoryContext();
  if (proto_config.has_pipe()) {
    envoy::config::core::v3::Address address;
    *address.mutable_pipe() = proto_config.pipe();
    destination = THROW_OR_RETURN_VALUE(Network::Address::resolveProtoAddress(address),
                                        Network::Address::InstanceConstSharedPtr);
  } else {
    const absl::Status status =
        server_context.clusterManager().checkActiveStaticCluster(proto_config.cluster());
    if (!status.ok()) {
      throw EnvoyException(
          fmt::format("syslog cluster '{}' must refer to an active static cluster: {}",
                      proto_config.cluster(), status.message()));
    }
    const auto cluster = server_context.clusterManager().getActiveCluster(proto_config.cluster());
    if (!cluster.has_value()) {
      throw EnvoyException(fmt::format("cluster '{}' is not active", proto_config.cluster()));
    }
  }
  auto shared_config = std::make_shared<SyslogAccessLogConfig>(proto_config);

  return std::make_shared<SyslogAccessLog>(
      std::move(filter), std::move(formatter), std::move(shared_config), std::move(destination),
      server_context.threadLocal(), server_context.serverScope(), server_context.clusterManager());
}

ProtobufTypes::MessagePtr SyslogAccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<SyslogAccessLogConfig>();
}

std::string SyslogAccessLogFactory::name() const { return "envoy.access_loggers.syslog"; }

/**
 * Static registration for the syslog access log. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(SyslogAccessLogFactory, Envoy::AccessLog::AccessLogInstanceFactory,
                        "envoy.syslog_access_log");

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
