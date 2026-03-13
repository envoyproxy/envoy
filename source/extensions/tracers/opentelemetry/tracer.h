#pragma once

#include <cstdint>
#include <string>

#include "envoy/api/api.h"
#include "envoy/common/optref.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/common/factory_base.h"
#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "absl/strings/escaping.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

#define OPENTELEMETRY_TRACER_STATS(COUNTER)                                                        \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)                                                                           \
  COUNTER(spans_dropped)

struct OpenTelemetryTracerStats {
  OPENTELEMETRY_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

class Span;

/**
 * OpenTelemetry Tracer. It is stored in TLS and contains the exporter.
 */
class Tracer : Logger::Loggable<Logger::Id::tracing> {
public:
  Tracer(OpenTelemetryTraceExporterPtr exporter, Envoy::TimeSource& time_source,
         Random::RandomGenerator& random, Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
         OpenTelemetryTracerStats tracing_stats, const ResourceConstSharedPtr resource,
         SamplerSharedPtr sampler, uint64_t max_cache_size,
         std::shared_ptr<ResourceProvider> resource_provider);

  void sendSpan(::opentelemetry::proto::trace::v1::Span&& span);
  void sendSpanWithResource(ResourceConstSharedPtr resource,
                            ::opentelemetry::proto::trace::v1::Span&& span);

  std::unique_ptr<Span> startSpan(const std::string& operation_name,
                                  const StreamInfo::StreamInfo& stream_info, SystemTime start_time,
                                  Tracing::Decision tracing_decision,
                                  OptRef<const Tracing::TraceContext> trace_context,
                                  OTelSpanKind span_kind);

  std::unique_ptr<Span> startSpan(const std::string& operation_name,
                                  const StreamInfo::StreamInfo& stream_info, SystemTime start_time,
                                  const SpanContext& previous_span_context,
                                  OptRef<const Tracing::TraceContext> trace_context,
                                  OTelSpanKind span_kind);

  const ResourceProvider& resourceProvider() const { return *resource_provider_; }

private:
  /**
   * Enables the span-flushing timer.
   */
  void enableTimer();
  /*
   * Removes all spans from the span buffer and sends them to the collector.
   */
  void flushSpans();

  OpenTelemetryTraceExporterPtr exporter_;
  Envoy::TimeSource& time_source_;
  Random::RandomGenerator& random_;
  std::vector<::opentelemetry::proto::trace::v1::Span> span_buffer_;
  absl::flat_hash_map<ResourceConstSharedPtr, std::vector<::opentelemetry::proto::trace::v1::Span>>
      per_tenant_span_buffer_;
  Runtime::Loader& runtime_;
  Event::TimerPtr flush_timer_;
  OpenTelemetryTracerStats tracing_stats_;
  const ResourceConstSharedPtr resource_;
  const std::shared_ptr<ResourceProvider> resource_provider_;
  SamplerSharedPtr sampler_;
  uint64_t max_cache_size_;
  uint64_t span_count_ = 0;
};

/**
 * OpenTelemetry tracing implementation of the Envoy Span object.
 * Note that it has a pointer to its parent Tracer to access the shared Exporter.
 */
class Span : Logger::Loggable<Logger::Id::tracing>, public Tracing::Span {
public:
  Span(const std::string& name, const StreamInfo::StreamInfo& stream_info, SystemTime start_time,
       Envoy::TimeSource& time_source, Tracer& parent_tracer, OTelSpanKind span_kind,
       bool use_local_decision = false);

  // Tracing::Span functions
  void setOperation(absl::string_view /*operation*/) override;
  void setTag(absl::string_view /*name*/, absl::string_view /*value*/) override;
  void log(SystemTime /*timestamp*/, const std::string& /*event*/) override;
  void finishSpan() override;
  void injectContext(Envoy::Tracing::TraceContext& /*trace_context*/,
                     const Tracing::UpstreamContext&) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;

  /**
   * Set the span's sampled flag.
   */
  void setSampled(bool sampled) override { sampled_ = sampled; };

  /**
   * @return whether the local tracing decision is used by the span.
   */
  bool useLocalDecision() const override { return use_local_decision_; }

  /**
   * @return whether or not the sampled attribute is set
   */
  bool sampled() const { return sampled_; }

  std::string getBaggage(absl::string_view /*key*/) override { return EMPTY_STRING; };
  void setBaggage(absl::string_view /*key*/, absl::string_view /*value*/) override{};

  // Additional methods

  /**
   * Sets the span's trace id attribute.
   */
  void setTraceId(absl::string_view trace_id_hex) {
    span_.set_trace_id(absl::HexStringToBytes(trace_id_hex));
  }

  std::string getTraceId() const override { return absl::BytesToHexString(span_.trace_id()); };

  std::string getSpanId() const override { return absl::BytesToHexString(span_.span_id()); };

  OTelSpanKind spankind() const { return span_.kind(); }

  /**
   * @return the operation name set on the span
   */
  absl::string_view name() const { return span_.name(); }

  /**
   * Sets the span's id.
   */
  void setId(absl::string_view span_id_hex) {
    span_.set_span_id(absl::HexStringToBytes(span_id_hex));
  }

  std::string spanId() { return absl::BytesToHexString(span_.span_id()); }

  /**
   * Sets the span's parent id.
   */
  void setParentId(absl::string_view parent_span_id_hex) {
    span_.set_parent_span_id(absl::HexStringToBytes(parent_span_id_hex));
  }

  absl::string_view tracestate() const { return span_.trace_state(); }

  /**
   * Sets the span's tracestate.
   */
  void setTracestate(absl::string_view tracestate) {
    span_.set_trace_state(std::string{tracestate});
  }

  /**
   * Sets a span attribute.
   */
  void setAttribute(absl::string_view name, const OTelAttribute& value);

  /**
   * Method to access the span for testing.
   */
  const ::opentelemetry::proto::trace::v1::Span& spanForTest() const { return span_; }

  void setResource(ResourceConstSharedPtr resource) { resource_ = resource; }

private:
  ::opentelemetry::proto::trace::v1::Span span_;
  const StreamInfo::StreamInfo& stream_info_;
  Tracer& parent_tracer_;
  Envoy::TimeSource& time_source_;
  ResourceConstSharedPtr resource_;
  bool sampled_;
  const bool use_local_decision_{false};
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
