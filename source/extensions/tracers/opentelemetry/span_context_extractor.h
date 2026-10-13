#pragma once

#include <string>

#include "envoy/common/exception.h"
#include "envoy/tracing/tracer.h"

#include "source/common/common/statusor.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class OpenTelemetryConstantValues {
public:
  const Tracing::TraceContextHandler TRACE_PARENT{"traceparent"};
  const Tracing::TraceContextHandler TRACE_STATE{"tracestate"};
};

using OpenTelemetryConstants = ConstSingleton<OpenTelemetryConstantValues>;

/**
 * This class is used to extract a SpanContext from the HTTP traceparent header
 * or MCP body. See https://www.w3.org/TR/trace-context/#traceparent-header.
 */
class SpanContextExtractor {
public:
  SpanContextExtractor(Tracing::TraceContext& trace_context);
  SpanContextExtractor(const Tracing::TraceContext& trace_context);
  ~SpanContextExtractor();
  absl::StatusOr<SpanContext> extractSpanContext();
  bool propagationHeaderPresent();

  // Parse the given `traceparent` to `SpanContext`, forwarding the given
  // `tracestate` to its constructor.
  static absl::StatusOr<SpanContext> parseTraceparent(absl::string_view traceparent,
                                                      std::string tracestate);
  // Serialize the given `span_context` to a traceparent string. This omits any
  // tracestate that may be present on the `span_context. Performs no
  // validation `span_context`.
  static std::string serializeTraceparent(const SpanContext& span_context);

private:
  const Tracing::TraceContext& trace_context_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
