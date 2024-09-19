#include "source/extensions/access_loggers/fluentd/config.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/fluentd/fluentd_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(fluentd_access_logger_cache);

FluentdAccessLoggerCacheSharedPtr
getAccessLoggerCacheSingleton(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<FluentdAccessLoggerCacheImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(fluentd_access_logger_cache),
      [&context] {
        return std::make_shared<FluentdAccessLoggerCacheImpl>(
            context.clusterManager(), context.scope(), context.threadLocal());
      },
      /* pin = */ true);
}

AccessLog::InstanceSharedPtr
FluentdAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                                 AccessLog::FilterPtr&& filter,
                                                 Server::Configuration::FactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig&>(
      config, context.messageValidationVisitor());

  absl::Status status = context.serverFactoryContext().clusterManager().checkActiveStaticCluster(
      proto_config.cluster());
  if (!status.ok()) {
    throw EnvoyException(fmt::format("cluster '{}' was not found", proto_config.cluster()));
  }

  if (proto_config.has_retry_options() && proto_config.retry_options().has_backoff_options()) {
    status = BackOffStrategyUtils::validateBackOffStrategyConfig(
        proto_config.retry_options().backoff_options(), DefaultBaseBackoffIntervalMs,
        DefaultMaxBackoffIntervalFactor);
    if (!status.ok()) {
      throw EnvoyException(
          "max_backoff_interval must be greater or equal to base_backoff_interval");
    }
  }

  // Supporting nested object serialization is more complex with MessagePack.
  // Using an already existing JSON formatter, and later converting the JSON string to a msgpack
  // payload.
  // TODO(ohadvano): Improve the formatting operation by creating a dedicated formatter that
  //                 will directly serialize the record to msgpack payload.
  auto commands = THROW_OR_RETURN_VALUE(
      Formatter::SubstitutionFormatStringUtils::parseFormatters(proto_config.formatters(), context),
      std::vector<Formatter::CommandParserBasePtr<Formatter::HttpFormatterContext>>);

  Formatter::FormatterPtr json_formatter =
      Formatter::SubstitutionFormatStringUtils::createJsonFormatter(proto_config.record(), true,
                                                                    false, false, commands);
  FluentdFormatterPtr fluentd_formatter =
      std::make_unique<FluentdFormatterImpl>(std::move(json_formatter));

  return std::make_shared<FluentdAccessLog>(
      std::move(filter), std::move(fluentd_formatter),
      std::make_shared<FluentdAccessLogConfig>(proto_config),
      context.serverFactoryContext().threadLocal(),
      context.serverFactoryContext().api().randomGenerator(),
      getAccessLoggerCacheSingleton(context.serverFactoryContext()));
}

ProtobufTypes::MessagePtr FluentdAccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::access_loggers::fluentd::v3::FluentdAccessLogConfig>();
}

std::string FluentdAccessLogFactory::name() const { return "envoy.access_loggers.fluentd"; }

/**
 * Static registration for the fluentd access log. @see RegisterFactory.
 */
REGISTER_FACTORY(FluentdAccessLogFactory,
                 AccessLog::AccessLogInstanceFactory){"envoy.fluentd_access_log"};

} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
