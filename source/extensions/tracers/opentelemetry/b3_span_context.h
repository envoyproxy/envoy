#pragma once

#include <optional>
#include <string>

#include "envoy/tracing/trace_context.h"

#include "source/common/singleton/const_singleton.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class B3ConstantValues {
public:
  const Tracing::TraceContextHandler B3{"b3"};
  const Tracing::TraceContextHandler X_B3_TRACE_ID{"x-b3-traceid"};
  const Tracing::TraceContextHandler X_B3_SPAN_ID{"x-b3-spanid"};
  const Tracing::TraceContextHandler X_B3_PARENT_SPAN_ID{"x-b3-parentspanid"};
  const Tracing::TraceContextHandler X_B3_SAMPLED{"x-b3-sampled"};
  const Tracing::TraceContextHandler X_B3_FLAGS{"x-b3-flags"};
};

using B3Constants = ConstSingleton<B3ConstantValues>;

/**
 * Trace context extracted from B3 headers.
 * See https://github.com/openzipkin/b3-propagation.
 */
struct B3SpanContext {
  // 128-bit trace id as 32 hex characters. 64-bit trace ids are left-padded with zeros.
  std::string trace_id;
  // 64-bit span id as 16 hex characters.
  std::string span_id;
  // The sampling decision, or std::nullopt if the sender deferred the decision.
  std::optional<bool> sampled;
};

/**
 * @return true if the trace context carries B3 trace and span ids in either the single header or
 * the multiple header encoding. A single header that only carries a sampling decision is not
 * considered to be present.
 */
bool b3HeadersPresent(const Tracing::TraceContext& trace_context);

/**
 * Extracts B3 trace context. The single header encoding takes precedence over the multiple header
 * encoding; if the single header is malformed, the multiple header encoding is tried.
 * @return the extracted context or an error if no valid B3 trace context is present.
 */
absl::StatusOr<B3SpanContext> extractB3SpanContext(const Tracing::TraceContext& trace_context);

/**
 * Writes the B3 single header.
 */
void injectB3SingleHeader(Tracing::TraceContext& trace_context, absl::string_view trace_id,
                          absl::string_view span_id, bool sampled);

/**
 * Writes the B3 multiple headers. Any existing x-b3-parentspanid and x-b3-flags headers are
 * removed because they would describe a different span.
 */
void injectB3MultiHeaders(Tracing::TraceContext& trace_context, absl::string_view trace_id,
                          absl::string_view span_id, bool sampled);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
