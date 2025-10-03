#pragma once

#include "envoy/common/exception.h"

#include "source/common/common/statusor.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"
#include "source/extensions/propagators/opentelemetry/propagator.h"

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
 * This class is used to extract SpanContext from HTTP headers using configured propagators.
 * Supports multiple propagation formats (W3C, B3, etc.) based on configuration.
 */
class SpanContextExtractor {
public:
  SpanContextExtractor(Tracing::TraceContext& trace_context,
                       Propagators::OpenTelemetry::CompositePropagatorPtr propagator);
  ~SpanContextExtractor();
  absl::StatusOr<SpanContext> extractSpanContext();
  bool propagationHeaderPresent();

private:
  const Tracing::TraceContext& trace_context_;
  Propagators::OpenTelemetry::CompositePropagatorPtr propagator_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
