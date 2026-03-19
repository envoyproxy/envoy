#include "source/extensions/access_loggers/dynamic_modules/config.h"

#include "envoy/extensions/access_loggers/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

AccessLog::InstanceSharedPtr DynamicModuleAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::GenericFactoryContext& context,
    std::vector<Formatter::CommandParserPtr>&&) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog&>(
      config, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());

  if (!dynamic_module_or_error.ok()) {
    throw EnvoyException("Failed to load dynamic module: " +
                         std::string(dynamic_module_or_error.status().message()));
  }

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string logger_config_str;
  if (proto_config.has_logger_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.logger_config());
    if (!config_or_error.ok()) {
      throw EnvoyException("Failed to parse logger config: " +
                           std::string(config_or_error.status().message()));
    }
    logger_config_str = std::move(config_or_error.value());
  }

  // Use configured metrics namespace or fall back to the default.
  const std::string metrics_namespace = module_config.metrics_namespace().empty()
                                            ? std::string(DefaultMetricsNamespace)
                                            : module_config.metrics_namespace();

  auto access_log_config = newDynamicModuleAccessLogConfig(
      proto_config.logger_name(), logger_config_str, metrics_namespace,
      std::move(dynamic_module_or_error.value()), context.serverFactoryContext().scope());

  if (!access_log_config.ok()) {
    throw EnvoyException("Failed to create access logger config: " +
                         std::string(access_log_config.status().message()));
  }

  // When the runtime guard is enabled, register the metrics namespace as a custom stat namespace.
  // This causes the namespace prefix to be stripped from prometheus output and no envoy_ prefix
  // is added. This is the legacy behavior for backward compatibility.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    context.serverFactoryContext().api().customStatNamespaces().registerStatNamespace(
        metrics_namespace);
  }

  return std::make_shared<DynamicModuleAccessLog>(std::move(filter),
                                                  std::move(access_log_config.value()),
                                                  context.serverFactoryContext().threadLocal());
}

ProtobufTypes::MessagePtr DynamicModuleAccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::access_loggers::dynamic_modules::v3::DynamicModuleAccessLog>();
}

REGISTER_FACTORY(DynamicModuleAccessLogFactory, AccessLog::AccessLogInstanceFactory);

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
