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

DatadogTracerFactory::DatadogTracerFactory() : FactoryBase(TracerNames::get().Datadog) {}

Tracing::DriverPtr
DatadogTracerFactory::createDriverTyped(const envoy::config::trace::v2::DatadogConfig& proto_config,
                                        Server::Instance& server) {
  return std::make_unique<Driver>(proto_config, server.clusterManager(), server.stats(),
                                  server.threadLocal(), server.runtime());
}

/**
 * Static registration for the Datadog tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(DatadogTracerFactory, Server::Configuration::TracerFactory);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
