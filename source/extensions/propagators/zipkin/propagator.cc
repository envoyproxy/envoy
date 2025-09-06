#include "source/extensions/propagators/zipkin/propagator.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {

CompositePropagator::CompositePropagator(std::vector<TextMapPropagatorPtr> propagators)
    : propagators_(std::move(propagators)) {
  ENVOY_LOG(debug, "Initialized Zipkin CompositePropagator with {} propagators",
            propagators_.size());
}

absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
CompositePropagator::extract(const Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    auto result = propagator->extract(trace_context);
    if (result.ok()) {
      ENVOY_LOG(debug, "Successfully extracted span context using propagator: {}",
                propagator->name());
      return result;
    }
    ENVOY_LOG(debug, "Failed to extract span context using propagator: {}, error: {}",
              propagator->name(), result.status().message());
  }

  return absl::NotFoundError("No propagator could extract span context from headers");
}

void CompositePropagator::inject(const Extensions::Tracers::Zipkin::SpanContext& span_context,
                                 Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    try {
      propagator->inject(span_context, trace_context);
      ENVOY_LOG(debug, "Successfully injected span context using propagator: {}",
                propagator->name());
    } catch (const std::exception& e) {
      ENVOY_LOG(warn, "Failed to inject span context using propagator: {}, error: {}",
                propagator->name(), e.what());
    }
  }
}

bool CompositePropagator::propagationHeaderPresent(const Tracing::TraceContext& trace_context) {
  for (const auto& propagator : propagators_) {
    for (const auto& field : propagator->fields()) {
      if (trace_context.getByKey(field).has_value()) {
        ENVOY_LOG(debug, "Found propagation header '{}' for propagator: {}", field,
                  propagator->name());
        return true;
      }
    }
  }
  return false;
}

} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
