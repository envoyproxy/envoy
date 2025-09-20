#pragma once

#include "source/extensions/propagators/opentelemetry/propagator.h"
#include "source/common/tracing/trace_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace OpenTelemetry {

/**
 * W3C Trace Context propagator.
 * Handles traceparent and tracestate headers.
 * See: https://www.w3.org/TR/trace-context/
 */
class W3CTraceContextPropagator : public TextMapPropagator {
public:
  W3CTraceContextPropagator();

  // TextMapPropagator
  absl::StatusOr<SpanContext> extract(const Tracing::TraceContext& trace_context) override;
  void inject(const SpanContext& span_context, Tracing::TraceContext& trace_context) override;
  std::vector<std::string> fields() const override;
  std::string name() const override;

private:
  const Tracing::TraceContextHandler trace_parent_header_;
  const Tracing::TraceContextHandler trace_state_header_;
};

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
