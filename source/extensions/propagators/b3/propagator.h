#pragma once

#include "source/extensions/propagators/generic_propagator.h"
#include "source/common/tracing/trace_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace B3 {

/**
 * B3 propagator supporting both single header (b3) and multi-header (X-B3-*) formats.
 * This implementation is fully compliant with the B3 specification and is tracer-agnostic.
 *
 * Handles all B3 specification features:
 * - Single header format: {TraceId}-{SpanId}-{SamplingState}-{ParentSpanId}
 * - Multi-header format: X-B3-TraceId, X-B3-SpanId, X-B3-Sampled, X-B3-Flags, X-B3-ParentSpanId
 * - Debug flag support: "d" implies sampling in both single and multi-header formats
 * - Sampling-only headers: "0", "1", "d" without trace context return appropriate errors
 * - Parent span ID support: Parent IDs are included in the generic SpanContext
 *
 * Auto-detects format on extraction, injects both formats for maximum compatibility.
 *
 * See: https://github.com/openzipkin/b3-propagation
 */
class B3Propagator : public GenericPropagator {
public:
  B3Propagator();

  // GenericPropagator interface
  absl::StatusOr<SpanContext> extract(const Tracing::TraceContext& trace_context) override;
  void inject(const SpanContext& span_context, Tracing::TraceContext& trace_context) override;
  std::vector<std::string> fields() const override;
  std::string name() const override;

private:
  // Single header format
  const Tracing::TraceContextHandler b3_header_;

  // Multi-header format
  const Tracing::TraceContextHandler x_b3_trace_id_header_;
  const Tracing::TraceContextHandler x_b3_span_id_header_;
  const Tracing::TraceContextHandler x_b3_sampled_header_;
  const Tracing::TraceContextHandler x_b3_flags_header_;
  const Tracing::TraceContextHandler x_b3_parent_span_id_header_;

  absl::StatusOr<SpanContext> extractSingleHeader(const Tracing::TraceContext& trace_context);
  absl::StatusOr<SpanContext> extractMultiHeader(const Tracing::TraceContext& trace_context);

  void injectSingleHeader(const SpanContext& span_context, Tracing::TraceContext& trace_context);
  void injectMultiHeader(const SpanContext& span_context, Tracing::TraceContext& trace_context);
};

} // namespace B3
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
