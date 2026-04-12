#include "source/extensions/tracers/dynamic_modules/config.h"

#include "envoy/extensions/tracers/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/tracers/dynamic_modules/tracer_config.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicModules {

Tracing::DriverSharedPtr DynamicModuleTracerFactory::createTracerDriver(
    const Protobuf::Message& config, Server::Configuration::TracerFactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer&>(
      config, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());

  if (!dynamic_module_or_error.ok()) {
    throw EnvoyException("Failed to load dynamic module: " +
                         std::string(dynamic_module_or_error.status().message()));
  }

  std::string tracer_config_str;
  if (proto_config.has_tracer_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.tracer_config());
    if (!config_or_error.ok()) {
      throw EnvoyException("Failed to parse tracer config: " +
                           std::string(config_or_error.status().message()));
    }
    tracer_config_str = std::move(config_or_error.value());
  }

  const std::string metrics_namespace = module_config.metrics_namespace().empty()
                                            ? std::string(DefaultMetricsNamespace)
                                            : module_config.metrics_namespace();

  auto tracer_config = newDynamicModuleTracerConfig(
      proto_config.tracer_name(), tracer_config_str, metrics_namespace,
      std::move(dynamic_module_or_error.value()), context.serverFactoryContext().scope());

  if (!tracer_config.ok()) {
    throw EnvoyException("Failed to create tracer config: " +
                         std::string(tracer_config.status().message()));
  }

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    context.serverFactoryContext().api().customStatNamespaces().registerStatNamespace(
        metrics_namespace);
  }

  return std::make_shared<DynamicModuleDriver>(std::move(tracer_config.value()));
}

ProtobufTypes::MessagePtr DynamicModuleTracerFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer>();
}

REGISTER_FACTORY(DynamicModuleTracerFactory, Server::Configuration::TracerFactory);

} // namespace DynamicModules
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
