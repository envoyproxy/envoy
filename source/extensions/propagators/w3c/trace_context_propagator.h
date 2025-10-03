#pragma once

#include "source/extensions/propagators/generic_propagator.h"
#include "source/common/tracing/trace_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {

/**
 * W3C Trace Context propagator that is tracer-agnostic.
 * This implementation is fully compliant with the W3C Trace Context specification.
 *
 * Handles W3C Trace Context specification features:
 * - traceparent header: 00-{trace-id}-{parent-id}-{trace-flags}
 * - tracestate header: vendor-specific key-value pairs
 * - Version validation and future version compatibility
 * - Proper trace flag handling for sampling decisions
 *
 * See: https://www.w3.org/TR/trace-context/
 */
class TraceContextPropagator : public GenericPropagator {
public:
  TraceContextPropagator();

  // GenericPropagator interface
  absl::StatusOr<SpanContext> extract(const Tracing::TraceContext& trace_context) override;
  void inject(const SpanContext& span_context, Tracing::TraceContext& trace_context) override;
  std::vector<std::string> fields() const override;
  std::string name() const override;

private:
  const Tracing::TraceContextHandler traceparent_header_;
  const Tracing::TraceContextHandler tracestate_header_;

  absl::StatusOr<SpanContext> parseTraceparent(const std::string& traceparent_value,
                                               const std::string& tracestate_value);
  std::string formatTraceparent(const SpanContext& span_context);
};

} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
