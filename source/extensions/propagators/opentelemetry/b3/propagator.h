#pragma once

#include "source/extensions/propagators/opentelemetry/propagator.h"
#include "source/common/tracing/trace_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace OpenTelemetry {

/**
 * Zipkin B3 propagator.
 * Supports both single header (b3) and multi-header (X-B3-*) formats.
 * Auto-detects format on extraction, injects both formats.
 * See: https://github.com/openzipkin/b3-propagation
 */
class B3Propagator : public TextMapPropagator {
public:
  B3Propagator();

  // TextMapPropagator
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
};

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
