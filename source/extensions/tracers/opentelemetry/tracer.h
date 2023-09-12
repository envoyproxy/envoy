#pragma once

#include <cstdint>
#include <memory>

#include "envoy/api/api.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/common/factory_base.h"
#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"

#include "absl/strings/escaping.h"
#include "span_context.h"

#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

#define OPENTELEMETRY_TRACER_STATS(COUNTER)                                                        \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)

struct OpenTelemetryTracerStats {
  OPENTELEMETRY_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * OpenTelemetry Tracer. It is stored in TLS and contains the exporter.
 */
class Tracer : Logger::Loggable<Logger::Id::tracing> {
public:
  Tracer(OpenTelemetryGrpcTraceExporterPtr exporter, Envoy::TimeSource& time_source,
         Random::RandomGenerator& random, Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
         OpenTelemetryTracerStats tracing_stats, const std::string& service_name, SamplerPtr sampler);

  void sendSpan(::opentelemetry::proto::trace::v1::Span& span);

  Tracing::SpanPtr startSpan(const Tracing::Config& config, const std::string& operation_name,
                             SystemTime start_time, const Tracing::Decision tracing_decision);

  Tracing::SpanPtr startSpan(const Tracing::Config& config, const std::string& operation_name,
                             SystemTime start_time, const SpanContext& previous_span_context);

private:
  /**
   * Enables the span-flushing timer.
   */
  void enableTimer();
  /*
   * Removes all spans from the span buffer and sends them to the collector.
   */
  void flushSpans();

  OpenTelemetryGrpcTraceExporterPtr exporter_;
  Envoy::TimeSource& time_source_;
  Random::RandomGenerator& random_;
  std::vector<::opentelemetry::proto::trace::v1::Span> span_buffer_;
  Runtime::Loader& runtime_;
  Event::TimerPtr flush_timer_;
  OpenTelemetryTracerStats tracing_stats_;
  std::string service_name_;
  SamplerPtr sampler_;
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
