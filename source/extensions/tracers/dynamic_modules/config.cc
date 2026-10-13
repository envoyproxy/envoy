#include "source/extensions/tracers/dynamic_modules/config.h"

#include "envoy/extensions/tracers/dynamic_modules/v3/dynamic_modules.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
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

  Server::Configuration::ServerFactoryContext& server_context = context.serverFactoryContext();
  const auto& module_config = proto_config.dynamic_module_config();
  // Tracers do not support remote module sources, so no init manager or async callback is passed;
  // only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.tracer_name(), context.serverFactoryContext());
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }
  auto dynamic_module = std::move(load_result->loaded);

  std::string tracer_config_str;
  if (proto_config.has_tracer_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.tracer_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          server_context, proto_config.tracer_name(),
          Extensions::DynamicModules::ConfigInitErrorStat);
      throw EnvoyException("Failed to parse tracer config: " +
                           std::string(config_or_error.status().message()));
    }
    tracer_config_str = std::move(config_or_error.value());
  }

  const std::string metrics_namespace = module_config.metrics_namespace().empty()
                                            ? std::string(DefaultMetricsNamespace)
                                            : module_config.metrics_namespace();

  auto tracer_config =
      newDynamicModuleTracerConfig(proto_config.tracer_name(), tracer_config_str, metrics_namespace,
                                   std::move(dynamic_module), server_context.scope());

  if (!tracer_config.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        server_context, proto_config.tracer_name(),
        Extensions::DynamicModules::ConfigInitErrorStat);
    throw EnvoyException("Failed to create tracer config: " +
                         std::string(tracer_config.status().message()));
  }

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix")) {
    server_context.api().customStatNamespaces().registerStatNamespace(metrics_namespace);
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
