#include "source/extensions/tracers/zipkin/span_context_extractor.h"

#include <charconv>

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/extensions/propagators/zipkin/propagator_factory.h"
#include "source/extensions/tracers/zipkin/span_context.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

SpanContextExtractor::SpanContextExtractor(Tracing::TraceContext& trace_context,
                                           bool w3c_fallback_enabled)
    : trace_context_(trace_context), w3c_fallback_enabled_(w3c_fallback_enabled) {

  // Initialize composite propagator based on configuration
  std::vector<std::string> propagator_names = {"b3"};
  if (w3c_fallback_enabled) {
    propagator_names.push_back("tracecontext");
  }

  composite_propagator_ =
      Extensions::Propagators::Zipkin::PropagatorFactory::createPropagators(propagator_names);
}

SpanContextExtractor::SpanContextExtractor(Tracing::TraceContext& trace_context,
                                           bool w3c_fallback_enabled,
                                           const std::vector<std::string>& propagator_names)
    : trace_context_(trace_context), w3c_fallback_enabled_(w3c_fallback_enabled),
      propagator_names_(propagator_names) {

  // Use provided propagator names, with fallback to B3 if none specified
  std::vector<std::string> names =
      propagator_names.empty() ? std::vector<std::string>{"b3"} : propagator_names;
  composite_propagator_ =
      Extensions::Propagators::Zipkin::PropagatorFactory::createPropagators(names);
}

SpanContextExtractor::~SpanContextExtractor() = default;

absl::optional<bool> SpanContextExtractor::extractSampled() {
  if (!composite_propagator_->propagationHeaderPresent(trace_context_)) {
    return absl::nullopt;
  }

  auto result = composite_propagator_->extract(trace_context_);
  if (result.ok()) {
    return result.value().sampled();
  }

  // Handle special B3 sampling-only cases ("0", "1", "d")
  // These are valid B3 headers that indicate sampling state without full trace context
  auto b3_header = trace_context_.getByKey("b3");
  if (b3_header.has_value()) {
    const auto& header_value = b3_header.value();
    if (header_value == "0") {
      return false; // Explicitly not sampled
    } else if (header_value == "1" || header_value == "d") {
      return true; // Sampled or debug
    }
  }

  // Check for B3 sampled header
  auto sampled_header = trace_context_.getByKey("x-b3-sampled");
  if (sampled_header.has_value()) {
    const auto& sampled_value = sampled_header.value();
    if (sampled_value == "0") {
      return false;
    } else if (sampled_value == "1" || sampled_value == "d") {
      return true;
    }
  }

  return absl::nullopt;
}

std::pair<SpanContext, bool> SpanContextExtractor::extractSpanContext(bool is_sampled) {
  if (!composite_propagator_->propagationHeaderPresent(trace_context_)) {
    return {SpanContext(), false};
  }

  auto result = composite_propagator_->extract(trace_context_);
  if (result.ok()) {
    return {result.value(), true};
  }

  // If extraction failed but we know it's sampled, create a minimal context
  if (is_sampled) {
    return {SpanContext(0, 0, 0, 0, true), false};
  }

  return {SpanContext(), false};
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
