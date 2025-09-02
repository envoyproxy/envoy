#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SpanContextExtractor::SpanContextExtractor(
    Tracing::TraceContext& trace_context,
    Propagators::OpenTelemetry::CompositePropagatorPtr propagator)
    : trace_context_(trace_context), propagator_(std::move(propagator)) {}

SpanContextExtractor::~SpanContextExtractor() = default;

bool SpanContextExtractor::propagationHeaderPresent() {
  return propagator_->propagationHeaderPresent(trace_context_);
}

absl::StatusOr<SpanContext> SpanContextExtractor::extractSpanContext() {
  return propagator_->extract(trace_context_);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
