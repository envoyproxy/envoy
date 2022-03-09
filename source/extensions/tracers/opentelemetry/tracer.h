#pragma once

#include "envoy/api/api.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/tracing/trace_driver.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/runtime/runtime.h"

#include "source/common/common/logger.h"

#include "source/extensions/tracers/common/factory_base.h"

#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"
#include "span_context.h"
#include "absl/strings/escaping.h"
#include <cstdint>
#include <mutex>

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
         OpenTelemetryTracerStats tracing_stats);

  // TODO: maybe make this arg const
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
};

/**
 * OpenTelemetry tracing implementation of the Envoy Span object.
 * Note that it has a pointer to its parent Tracer to access the shared Exporter.
 */
class Span : Logger::Loggable<Logger::Id::tracing>, public Tracing::Span {
public:
  Span(const Tracing::Config& config, const std::string& name, SystemTime start_time,
       Envoy::TimeSource& time_source, Tracer& parent_tracer);

  // Tracing::Span functions
  void setOperation(absl::string_view /*operation*/) override{};
  void setTag(absl::string_view /*name*/, absl::string_view /*value*/) override{};
  void log(SystemTime /*timestamp*/, const std::string& /*event*/) override{};
  void finishSpan() override;
  void injectContext(Envoy::Tracing::TraceContext& /*trace_context*/) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;

  /**
   * Set the span's sampled flag.
   */
  void setSampled(bool sampled) override { sampled_ = sampled; };

  /**
   * @return whether or not the sampled attribute is set
   */

  bool sampled() const { return sampled_; }

  std::string getBaggage(absl::string_view /*key*/) override { return EMPTY_STRING; };
  void setBaggage(absl::string_view /*key*/, absl::string_view /*value*/) override{};
  std::string getTraceIdAsHex() const override { return EMPTY_STRING; };

  // Additional methods

  /**
   * Sets the span's trace id attribute.
   */
  void setTraceId(const std::string& trace_id_hex) {
    span_.set_trace_id(absl::HexStringToBytes(trace_id_hex));
  }

  std::string traceId() { return absl::BytesToHexString(span_.trace_id()); }

  /**
   * Sets the span's name attribute.
   */
  void setName(const std::string& val) { span_.set_name(val); }

  /**
   * Sets the span's id.
   */
  void setId(const std::string& span_id_hex) {
    span_.set_span_id(absl::HexStringToBytes(span_id_hex));
  }

  std::string spanId() { return absl::BytesToHexString(span_.span_id()); }

  /**
   * Sets the span's parent id.
   */
  void setParentId(const std::string& parent_span_id_hex) {
    span_.set_parent_span_id(absl::HexStringToBytes(parent_span_id_hex));
  }

private:
  ::opentelemetry::proto::trace::v1::Span span_;
  Tracer& parent_tracer_;
  Envoy::TimeSource& time_source_;
  bool sampled_;
};

using TracerPtr = std::unique_ptr<Tracer>;

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy