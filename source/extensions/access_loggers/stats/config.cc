#include "source/extensions/access_loggers/stats/config.h"

#include "envoy/extensions/access_loggers/stats/v3/stats.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/access_loggers/stats/stats.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

AccessLog::InstanceSharedPtr AccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::GenericFactoryContext& context,
    std::vector<Formatter::CommandParserPtr>&& command_parsers) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::access_loggers::stats::v3::Config&>(
          config, context.messageValidationVisitor());

  if (proto_config.stat_prefix().empty() == !proto_config.has_stats_scope()) {
    throw EnvoyException("Either 'stat_prefix' or 'stats_scope' must be configured, but not both.");
  }

  return std::make_shared<StatsAccessLog>(proto_config, context, std::move(filter),
                                          command_parsers);
}

ProtobufTypes::MessagePtr AccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::access_loggers::stats::v3::Config>();
}

std::string AccessLogFactory::name() const { return "envoy.access_loggers.stats"; }

REGISTER_FACTORY(AccessLogFactory, AccessLog::AccessLogInstanceFactory);

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
