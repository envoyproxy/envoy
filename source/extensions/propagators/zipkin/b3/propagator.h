#pragma once

#include "source/extensions/propagators/zipkin/propagator.h"
#include "source/extensions/tracers/common/utils/trace.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {

/**
 * B3 propagator implementation for Zipkin tracer.
 * Implements the B3 propagation specification while using Zipkin-specific types.
 *
 * Supports both single and multi-header B3 formats:
 * - Single: b3: {trace_id}-{span_id}-{sampling_state}-{parent_span_id}
 * - Multi: X-B3-TraceId, X-B3-SpanId, X-B3-ParentSpanId, X-B3-Sampled, X-B3-Flags
 *
 * Handles special B3 features:
 * - Debug sampling ("d" flag)
 * - Sampling-only headers ("0", "1", "d" without full context)
 * - Parent span ID extraction for Zipkin compatibility
 */
class B3Propagator : public TextMapPropagator {
public:
  B3Propagator() = default;

  // TextMapPropagator interface
  absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
  extract(const Tracing::TraceContext& trace_context) override;

  void inject(const Extensions::Tracers::Zipkin::SpanContext& span_context,
              Tracing::TraceContext& trace_context) override;

  std::vector<std::string> fields() const override;

  std::string name() const override { return "b3"; }

private:
  // B3 header names
  static constexpr absl::string_view B3_TRACE_ID_HEADER = "x-b3-traceid";
  static constexpr absl::string_view B3_SPAN_ID_HEADER = "x-b3-spanid";
  static constexpr absl::string_view B3_PARENT_SPAN_ID_HEADER = "x-b3-parentspanid";
  static constexpr absl::string_view B3_SAMPLED_HEADER = "x-b3-sampled";
  static constexpr absl::string_view B3_FLAGS_HEADER = "x-b3-flags";
  static constexpr absl::string_view B3_SINGLE_HEADER = "b3";

  // Helper methods
  absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
  extractSingleHeader(const Tracing::TraceContext& trace_context);

  absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
  extractMultiHeader(const Tracing::TraceContext& trace_context);

  void injectSingleHeader(const Extensions::Tracers::Zipkin::SpanContext& span_context,
                          Tracing::TraceContext& trace_context);

  void injectMultiHeader(const Extensions::Tracers::Zipkin::SpanContext& span_context,
                         Tracing::TraceContext& trace_context);

  absl::optional<bool> parseB3SamplingState(absl::string_view sampling_state);
};

} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
