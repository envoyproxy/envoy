#pragma once

#include "envoy/api/api.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/tracers/common/factory_base.h"
#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.h"
#include "source/extensions/tracers/opentelemetry/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * OpenTelemetry tracing driver.
 */
class Driver : Logger::Loggable<Logger::Id::tracing>, public Tracing::Driver {
public:
  Driver(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
         Server::Configuration::TracerFactoryContext& context);

  Driver(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
         Server::Configuration::TracerFactoryContext& context,
         const ResourceProvider& resource_provider);

  // Tracing::Driver
  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const StreamInfo::StreamInfo& stream_info,
                             const std::string& operation_name,
                             Tracing::Decision tracing_decision) override;

private:
  class TlsTracer : public ThreadLocal::ThreadLocalObject {
  public:
    TlsTracer(TracerPtr tracer);

    Tracer& tracer();

  private:
    TracerPtr tracer_;
  };

  const envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config_;
  ThreadLocal::SlotPtr tls_slot_ptr_;
  OpenTelemetryTracerStats tracing_stats_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
