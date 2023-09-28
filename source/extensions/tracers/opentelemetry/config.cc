#include "source/extensions/tracers/opentelemetry/config.h"

#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

OpenTelemetryTracerFactory::OpenTelemetryTracerFactory()
    : FactoryBase("envoy.tracers.opentelemetry") {}

Tracing::DriverSharedPtr OpenTelemetryTracerFactory::createTracerDriverTyped(
    const envoy::config::trace::v3::OpenTelemetryConfig& proto_config,
    Server::Configuration::TracerFactoryContext& context) {
  return std::make_shared<Driver>(proto_config, context);
}

/**
 * Static registration for the OpenTelemetry tracer. @see RegisterFactory.
 */
REGISTER_FACTORY(OpenTelemetryTracerFactory, Server::Configuration::TracerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
