#include "source/extensions/tracers/opentelemetry/tracer.h"

#include <cstdint>
#include <string>

#include "envoy/config/trace/v3/opentelemetry.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/hex.h"

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

constexpr absl::string_view kTraceParent = "traceparent";
constexpr absl::string_view kTraceState = "tracestate";
constexpr absl::string_view kDefaultVersion = "00";
constexpr absl::string_view kServiceNameKey = "service.name";
constexpr absl::string_view kDefaultServiceName = "unknown_service:envoy";

using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;

Span::Span(const Tracing::Config& config, const std::string& name, SystemTime start_time,
           Envoy::TimeSource& time_source, Tracer& parent_tracer)
    : parent_tracer_(parent_tracer), time_source_(time_source) {
  span_ = ::opentelemetry::proto::trace::v1::Span();
  if (config.operationName() == Tracing::OperationName::Egress) {
    span_.set_kind(::opentelemetry::proto::trace::v1::Span::SPAN_KIND_CLIENT);
  } else {
    span_.set_kind(::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER);
  }
  span_.set_name(name);
  span_.set_start_time_unix_nano(std::chrono::nanoseconds(start_time.time_since_epoch()).count());
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config& config, const std::string& name,
                                  SystemTime start_time) {
  // Build span_context from the current span, then generate the child span from that context.
  SpanContext span_context(kDefaultVersion, getTraceIdAsHex(), spanId(), sampled(), tracestate());
  return parent_tracer_.startSpan(config, name, start_time, span_context);
}

void Span::finishSpan() {
  // Call into the parent tracer so we can access the shared exporter.
  span_.set_end_time_unix_nano(
      std::chrono::nanoseconds(time_source_.systemTime().time_since_epoch()).count());
  if (sampled()) {
    parent_tracer_.sendSpan(span_);
  }
}

void Span::injectContext(Tracing::TraceContext& trace_context,
                         const Upstream::HostDescriptionConstSharedPtr&) {
  std::string trace_id_hex = absl::BytesToHexString(span_.trace_id());
  std::string span_id_hex = absl::BytesToHexString(span_.span_id());
  std::vector<uint8_t> trace_flags_vec{sampled()};
  std::string trace_flags_hex = Hex::encode(trace_flags_vec);
  std::string traceparent_header_value =
      absl::StrCat(kDefaultVersion, "-", trace_id_hex, "-", span_id_hex, "-", trace_flags_hex);
  // Set the traceparent in the trace_context.
  trace_context.setByReferenceKey(kTraceParent, traceparent_header_value);
  // Also set the tracestate.
  trace_context.setByReferenceKey(kTraceState, span_.trace_state());
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  // The attribute key MUST be a non-null and non-empty string.
  if (name.empty()) {
    return;
  }
  // Attribute keys MUST be unique.
  // If a value already exists for this key, overwrite it.
  for (auto& key_value : *span_.mutable_attributes()) {
    if (key_value.key() == name) {
      key_value.mutable_value()->set_string_value(std::string{value});
      return;
    }
  }
  // If we haven't found an existing match already, we can add a new key/value.
  opentelemetry::proto::common::v1::KeyValue key_value =
      opentelemetry::proto::common::v1::KeyValue();
  opentelemetry::proto::common::v1::AnyValue value_proto =
      opentelemetry::proto::common::v1::AnyValue();
  value_proto.set_string_value(std::string{value});
  key_value.set_key(std::string{name});
  *key_value.mutable_value() = value_proto;
  *span_.add_attributes() = key_value;
}

Tracer::Tracer(OpenTelemetryGrpcTraceExporterPtr exporter, Envoy::TimeSource& time_source,
               Random::RandomGenerator& random, Runtime::Loader& runtime,
               Event::Dispatcher& dispatcher, OpenTelemetryTracerStats tracing_stats,
               const std::string& service_name)
    : exporter_(std::move(exporter)), time_source_(time_source), random_(random), runtime_(runtime),
      tracing_stats_(tracing_stats), service_name_(service_name) {
  if (service_name.empty()) {
    service_name_ = std::string{kDefaultServiceName};
  }
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    tracing_stats_.timer_flushed_.inc();
    flushSpans();
    enableTimer();
  });
  enableTimer();
}

void Tracer::enableTimer() {
  const uint64_t flush_interval =
      runtime_.snapshot().getInteger("tracing.opentelemetry.flush_interval_ms", 5000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void Tracer::flushSpans() {
  ExportTraceServiceRequest request;
  // A request consists of ResourceSpans.
  ::opentelemetry::proto::trace::v1::ResourceSpans* resource_span = request.add_resource_spans();
  opentelemetry::proto::common::v1::KeyValue key_value =
      opentelemetry::proto::common::v1::KeyValue();
  opentelemetry::proto::common::v1::AnyValue value_proto =
      opentelemetry::proto::common::v1::AnyValue();
  value_proto.set_string_value(std::string{service_name_});
  key_value.set_key(std::string{kServiceNameKey});
  *key_value.mutable_value() = value_proto;
  (*resource_span->mutable_resource()->add_attributes()) = key_value;
  ::opentelemetry::proto::trace::v1::ScopeSpans* scope_span = resource_span->add_scope_spans();
  for (const auto& pending_span : span_buffer_) {
    (*scope_span->add_spans()) = pending_span;
  }
  tracing_stats_.spans_sent_.add(span_buffer_.size());
  if (!exporter_->log(request)) {
    // TODO: should there be any sort of retry or reporting here?
    ENVOY_LOG(trace, "Unsuccessful log request to OpenTelemetry trace collector.");
  }
  span_buffer_.clear();
}

void Tracer::sendSpan(::opentelemetry::proto::trace::v1::Span& span) {
  span_buffer_.push_back(span);
  const uint64_t min_flush_spans =
      runtime_.snapshot().getInteger("tracing.opentelemetry.min_flush_spans", 5U);
  if (span_buffer_.size() >= min_flush_spans) {
    flushSpans();
  }
}

Tracing::SpanPtr Tracer::startSpan(const Tracing::Config& config, const std::string& operation_name,
                                   SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  // Create an Tracers::OpenTelemetry::Span class that will contain the OTel span.
  Span new_span = Span(config, operation_name, start_time, time_source_, *this);
  new_span.setSampled(tracing_decision.traced);
  uint64_t trace_id_high = random_.random();
  uint64_t trace_id = random_.random();
  new_span.setTraceId(absl::StrCat(Hex::uint64ToHex(trace_id_high), Hex::uint64ToHex(trace_id)));
  uint64_t span_id = random_.random();
  new_span.setId(Hex::uint64ToHex(span_id));
  return std::make_unique<Span>(new_span);
}

Tracing::SpanPtr Tracer::startSpan(const Tracing::Config& config, const std::string& operation_name,
                                   SystemTime start_time,
                                   const SpanContext& previous_span_context) {
  // Create a new span and populate details from the span context.
  Span new_span = Span(config, operation_name, start_time, time_source_, *this);
  new_span.setSampled(previous_span_context.sampled());
  new_span.setTraceId(previous_span_context.traceId());
  if (!previous_span_context.parentId().empty()) {
    new_span.setParentId(previous_span_context.parentId());
  }
  // Generate a new identifier for the span id.
  uint64_t span_id = random_.random();
  new_span.setId(Hex::uint64ToHex(span_id));
  // Respect the previous span's sampled flag.
  new_span.setSampled(previous_span_context.sampled());
  if (!previous_span_context.tracestate().empty()) {
    new_span.setTracestate(std::string{previous_span_context.tracestate()});
  }
  return std::make_unique<Span>(new_span);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
