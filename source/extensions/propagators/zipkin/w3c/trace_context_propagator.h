#pragma once

#include "source/extensions/propagators/zipkin/propagator.h"
#include "source/extensions/tracers/common/utils/trace.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {

/**
 * W3C Trace Context propagator implementation for Zipkin tracer.
 * Implements the W3C Trace Context specification while using Zipkin-specific types.
 *
 * Handles W3C traceparent and tracestate headers:
 * - traceparent: version-trace_id-parent_id-trace_flags
 * - tracestate: vendor-specific state information
 *
 * Converts between W3C format and Zipkin SpanContext types.
 */
class W3CTraceContextPropagator : public TextMapPropagator {
public:
  W3CTraceContextPropagator() = default;

  // TextMapPropagator interface
  absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
  extract(const Tracing::TraceContext& trace_context) override;

  void inject(const Extensions::Tracers::Zipkin::SpanContext& span_context,
              Tracing::TraceContext& trace_context) override;

  std::vector<std::string> fields() const override;

  std::string name() const override { return "tracecontext"; }

private:
  // W3C header names
  static constexpr absl::string_view TRACEPARENT_HEADER = "traceparent";
  static constexpr absl::string_view TRACESTATE_HEADER = "tracestate";

  // Helper methods
  absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
  parseTraceparent(absl::string_view traceparent);

  std::string formatTraceparent(const Extensions::Tracers::Zipkin::SpanContext& span_context);
};

} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
