#include "source/extensions/propagators/generic_propagator.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {

// Default implementations for optional baggage methods
absl::StatusOr<Baggage> GenericPropagator::extractBaggage(const Tracing::TraceContext&) {
  return absl::UnimplementedError("Baggage extraction not supported by this propagator");
}

void GenericPropagator::injectBaggage(const Baggage&, Tracing::TraceContext&) {
  // Default implementation does nothing for propagators that don't support baggage
}

// GenericCompositePropagator implementation
GenericCompositePropagator::GenericCompositePropagator(
    std::vector<GenericPropagatorPtr> propagators)
    : propagators_(std::move(propagators)) {}

absl::StatusOr<SpanContext>
GenericCompositePropagator::extract(const Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    auto result = propagator->extract(trace_context);
    if (result.ok()) {
      ENVOY_LOG(trace, "Successfully extracted span context using {} propagator",
                propagator->name());
      return result;
    }
    ENVOY_LOG(trace, "Failed to extract span context using {} propagator: {}", propagator->name(),
              result.status().message());
  }

  return absl::NotFoundError("No propagator successfully extracted span context");
}

void GenericCompositePropagator::inject(const SpanContext& span_context,
                                        Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    propagator->inject(span_context, trace_context);
    ENVOY_LOG(trace, "Injected span context using {} propagator", propagator->name());
  }
}

absl::StatusOr<Baggage>
GenericCompositePropagator::extractBaggage(const Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    auto result = propagator->extractBaggage(trace_context);
    if (result.ok()) {
      ENVOY_LOG(trace, "Successfully extracted baggage using {} propagator", propagator->name());
      return result;
    }
    ENVOY_LOG(trace, "Failed to extract baggage using {} propagator: {}", propagator->name(),
              result.status().message());
  }

  return absl::NotFoundError("No propagator successfully extracted baggage");
}

void GenericCompositePropagator::injectBaggage(const Baggage& baggage,
                                               Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    propagator->injectBaggage(baggage, trace_context);
    ENVOY_LOG(trace, "Injected baggage using {} propagator", propagator->name());
  }
}

bool GenericCompositePropagator::propagationHeaderPresent(
    const Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    for (const auto& field : propagator->fields()) {
      if (trace_context.getByKey(field).has_value()) {
        ENVOY_LOG(trace, "Found propagation header '{}' for {} propagator", field,
                  propagator->name());
        return true;
      }
    }
  }
  return false;
}

} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
