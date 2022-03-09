#pragma once

#include <string>

#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/config/trace/v3/opentelemetry.pb.validate.h"

#include "source/extensions/tracers/common/factory_base.h"
#include "envoy/config/trace/v3/opentelemetry.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * Config registration for the OpenTelemetry tracer. @see TracerFactory.
 */
class OpenTelemetryTracerFactory
    : Logger::Loggable<Logger::Id::tracing>,
      public Common::FactoryBase<envoy::config::trace::v3::OpenTelemetryConfig> {
public:
  OpenTelemetryTracerFactory();

private:
  // FactoryBase
  Tracing::DriverSharedPtr
  createTracerDriverTyped(const envoy::config::trace::v3::OpenTelemetryConfig& proto_config,
                          Server::Configuration::TracerFactoryContext& context) override;

  Tracing::DriverSharedPtr driver_;
  envoy::config::trace::v3::OpenTelemetryConfig config_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
