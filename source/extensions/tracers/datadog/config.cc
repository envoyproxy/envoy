#include "source/extensions/tracers/datadog/config.h"

#include "envoy/config/trace/v3/datadog.pb.h"
#include "envoy/config/trace/v3/datadog.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"
#include "source/extensions/tracers/datadog/datadog_tracer_impl.h"

#include "datadog/opentracing.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

DatadogTracerFactory::DatadogTracerFactory() : FactoryBase("envoy.tracers.datadog") {}

Tracing::DriverSharedPtr DatadogTracerFactory::createTracerDriverTyped(
    const envoy::config::trace::v3::DatadogConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  return std::make_shared<Driver>(proto_config, context.serverFactoryContext().clusterManager(),
                                  context.serverFactoryContext().scope(),
                                  context.serverFactoryContext().threadLocal(),
                                  context.serverFactoryContext().runtime());
}

/**
 * Static registration for the Datadog tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(DatadogTracerFactory, Server::Configuration::TracerFactory);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
