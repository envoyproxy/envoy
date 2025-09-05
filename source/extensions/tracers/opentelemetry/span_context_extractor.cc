#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"

#include "envoy/tracing/tracer.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/propagators/w3c/propagator.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "absl/strings/escaping.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

// Helper to convert W3C trace context to OpenTelemetry span context
SpanContext convertFromW3C(const Propagators::W3C::TraceContext& w3c_context) {
  const auto& traceparent = w3c_context.traceParent();
  return SpanContext(traceparent.version(), traceparent.traceId(), traceparent.parentId(),
                     traceparent.isSampled(), w3c_context.traceState().toString());
}

} // namespace

SpanContextExtractor::SpanContextExtractor(Tracing::TraceContext& trace_context)
    : trace_context_(trace_context) {}

SpanContextExtractor::~SpanContextExtractor() = default;

bool SpanContextExtractor::propagationHeaderPresent() {
  return Propagators::W3C::Propagator::isPresent(trace_context_);
}

absl::StatusOr<SpanContext> SpanContextExtractor::extractSpanContext() {
  // Use W3C propagator to extract trace context
  auto w3c_result = Propagators::W3C::Propagator::extract(trace_context_);
  if (!w3c_result.ok()) {
    return w3c_result.status();
  }

  // Convert W3C trace context to OpenTelemetry span context
  return convertFromW3C(w3c_result.value());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
