#pragma once

#include "envoy/common/exception.h"
#include "envoy/tracing/tracer.h"

#include "source/common/common/statusor.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * This class is used to SpanContext extracted from the HTTP traceparent header
 * See https://www.w3.org/TR/trace-context/#traceparent-header.
 * Uses the W3C propagator component for extraction.
 */
class SpanContextExtractor {
public:
  SpanContextExtractor(Tracing::TraceContext& trace_context);
  ~SpanContextExtractor();
  absl::StatusOr<SpanContext> extractSpanContext();
  bool propagationHeaderPresent();

private:
  const Tracing::TraceContext& trace_context_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
