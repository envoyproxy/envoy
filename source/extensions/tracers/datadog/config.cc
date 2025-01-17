#include "source/extensions/tracers/datadog/config.h"

#include <datadog/runtime_id.h>
#include <datadog/tracer_config.h>

#include <memory>

#include "envoy/config/trace/v3/datadog.pb.h"
#include "envoy/config/trace/v3/datadog.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/version/version.h"
#include "source/extensions/tracers/datadog/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

DatadogTracerFactory::DatadogTracerFactory() : FactoryBase("envoy.tracers.datadog") {}

datadog::tracing::TracerConfig
DatadogTracerFactory::makeConfig(const envoy::config::trace::v3::DatadogConfig& proto_config) {
  datadog::tracing::TracerConfig config;
  config.version = "envoy " + Envoy::VersionInfo::version();
  config.name = "envoy.proxy";
  if (proto_config.service_name().empty()) {
    config.service = "envoy";
  } else {
    config.service = proto_config.service_name();
  }

  if (proto_config.has_remote_config()) {
    const auto& proto_remote_config = proto_config.remote_config();
    config.agent.remote_configuration_enabled = true;

    if (proto_remote_config.has_polling_interval()) {
      config.agent.remote_configuration_poll_interval_seconds =
          proto_remote_config.polling_interval().seconds();
    }
  } else {
    config.agent.remote_configuration_enabled = false;
  }

  config.integration_name = "envoy";
  config.integration_version = Envoy::VersionInfo::version();

  return config;
}

std::string DatadogTracerFactory::makeCollectorReferenceHost(
    const envoy::config::trace::v3::DatadogConfig& proto_config) {
  std::string collector_reference_host = proto_config.collector_hostname();
  if (collector_reference_host.empty()) {
    collector_reference_host = proto_config.collector_cluster();
  }
  return collector_reference_host;
}

Tracing::DriverSharedPtr DatadogTracerFactory::createTracerDriverTyped(
    const envoy::config::trace::v3::DatadogConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  auto& factory_context = context.serverFactoryContext();
  return std::make_shared<Tracer>(
      proto_config.collector_cluster(), makeCollectorReferenceHost(proto_config),
      makeConfig(proto_config), factory_context.clusterManager(), factory_context.scope(),
      factory_context.threadLocal(), factory_context.timeSource());
}

/**
 * Static registration for the Datadog tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(DatadogTracerFactory, Server::Configuration::TracerFactory);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
