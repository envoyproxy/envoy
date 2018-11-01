#include "extensions/tracers/datadog/config.h"

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/datadog/datadog_tracer_impl.h"
#include "extensions/tracers/well_known_names.h"

#include "datadog/opentracing.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

Tracing::HttpTracerPtr
DatadogTracerFactory::createHttpTracer(const envoy::config::trace::v2::Tracing& configuration,
                                       Server::Instance& server) {

  ProtobufTypes::MessagePtr config_ptr = createEmptyConfigProto();

  if (configuration.http().has_config()) {
    MessageUtil::jsonConvert(configuration.http().config(), *config_ptr);
  }

  const auto& datadog_config =
      dynamic_cast<const envoy::config::trace::v2::DatadogConfig&>(*config_ptr);

  Tracing::DriverPtr datadog_driver{
      std::make_unique<Driver>(datadog_config, server.clusterManager(), server.stats(),
                               server.threadLocal(), server.runtime())};
  return std::make_unique<Tracing::HttpTracerImpl>(std::move(datadog_driver), server.localInfo());
}

std::string DatadogTracerFactory::name() { return TracerNames::get().Datadog; }

/**
 * Static registration for the Datadog tracer. @see RegisterFactory.
 */
static Registry::RegisterFactory<DatadogTracerFactory, Server::Configuration::TracerFactory>
    register_;

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
