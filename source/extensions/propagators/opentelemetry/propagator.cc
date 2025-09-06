#include "source/extensions/propagators/opentelemetry/propagator.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace OpenTelemetry {

CompositePropagator::CompositePropagator(std::vector<TextMapPropagatorPtr> propagators)
    : propagators_(std::move(propagators)) {}

absl::StatusOr<SpanContext>
CompositePropagator::extract(const Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    auto result = propagator->extract(trace_context);
    if (result.ok()) {
      ENVOY_LOG(debug, "Successfully extracted span context using {} propagator",
                propagator->name());
      return result;
    }
    ENVOY_LOG(trace, "Failed to extract span context using {} propagator: {}", propagator->name(),
              result.status().message());
  }

  return absl::InvalidArgumentError("No propagator could extract span context");
}

void CompositePropagator::inject(const SpanContext& span_context,
                                 Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    propagator->inject(span_context, trace_context);
    ENVOY_LOG(trace, "Injected context using {} propagator", propagator->name());
  }
}

bool CompositePropagator::propagationHeaderPresent(const Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    // Check for fields that indicate trace context propagation
    // Both trace context and baggage propagators indicate propagation headers are present
    auto result = propagator->extract(trace_context);
    if (result.ok()) {
      return true;
    }
  }
  return false;
}

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
