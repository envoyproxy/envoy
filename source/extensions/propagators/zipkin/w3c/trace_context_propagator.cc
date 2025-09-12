#include "source/extensions/propagators/zipkin/w3c/trace_context_propagator.h"

#include "source/common/common/hex.h"
#include "source/common/common/utility.h"
#include "source/extensions/tracers/common/utils/trace.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {

absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
W3CTraceContextPropagator::extract(const Tracing::TraceContext& trace_context) {
  auto traceparent = trace_context.getByKey(TRACEPARENT_HEADER);
  if (!traceparent.has_value()) {
    return absl::NotFoundError("W3C traceparent header not found");
  }

  return parseTraceparent(traceparent.value());
}

void W3CTraceContextPropagator::inject(const Extensions::Tracers::Zipkin::SpanContext& span_context,
                                       Tracing::TraceContext& trace_context) {
  std::string traceparent = formatTraceparent(span_context);
  trace_context.setByKey(TRACEPARENT_HEADER, traceparent);

  // Note: We don't inject tracestate as Zipkin doesn't have vendor-specific state
  // that needs to be propagated in W3C format
}

std::vector<std::string> W3CTraceContextPropagator::fields() const {
  return {std::string(TRACEPARENT_HEADER), std::string(TRACESTATE_HEADER)};
}

absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
W3CTraceContextPropagator::parseTraceparent(absl::string_view traceparent) {
  std::vector<absl::string_view> parts = absl::StrSplit(traceparent, '-');
  if (parts.size() != 4) {
    return absl::InvalidArgumentError("Invalid W3C traceparent format");
  }

  // Parse version (should be "00")
  if (parts[0] != "00") {
    return absl::InvalidArgumentError("Unsupported W3C traceparent version");
  }

  // Parse trace ID (32 hex chars -> 128 bits)
  if (parts[1].length() != 32) {
    return absl::InvalidArgumentError("Invalid W3C trace ID length");
  }

  std::string trace_id_str(parts[1]);
  uint64_t trace_id_high = 0, trace_id_low = 0;
  if (!Extensions::Tracers::Common::Utils::Trace::parseTraceId(trace_id_str, trace_id_high,
                                                               trace_id_low)) {
    return absl::InvalidArgumentError("Invalid W3C trace ID format");
  }

  // Parse parent ID (16 hex chars -> 64 bits)
  if (parts[2].length() != 16) {
    return absl::InvalidArgumentError("Invalid W3C parent ID length");
  }

  uint64_t parent_id = 0;
  if (!Extensions::Tracers::Common::Utils::Trace::parseSpanId(std::string(parts[2]), parent_id)) {
    return absl::InvalidArgumentError("Invalid W3C parent ID format");
  }

  // Parse trace flags (2 hex chars -> 8 bits)
  if (parts[3].length() != 2) {
    return absl::InvalidArgumentError("Invalid W3C trace flags length");
  }

  uint64_t flags_value;
  if (!absl::SimpleHexAtoi(parts[3], &flags_value)) {
    return absl::InvalidArgumentError("Invalid W3C trace flags format");
  }

  bool sampled = (flags_value & 0x01) != 0; // Sampled flag is the least significant bit

  // For Zipkin, we generate a new span ID since the parent ID from W3C is the calling span
  // In Zipkin's model, this would be the parent_id, and we need to generate a new span ID
  uint64_t span_id = Extensions::Tracers::Common::Utils::Trace::generateRandom64();

  return Extensions::Tracers::Zipkin::SpanContext(trace_id_high, trace_id_low, span_id, parent_id,
                                                  sampled);
}

std::string W3CTraceContextPropagator::formatTraceparent(
    const Extensions::Tracers::Zipkin::SpanContext& span_context) {
  // Format: version-trace_id-parent_id-trace_flags
  std::string version = "00";

  // 128-bit trace ID (32 hex chars)
  std::string trace_id =
      Hex::uint64ToHex(span_context.traceIdHigh()) + Hex::uint64ToHex(span_context.traceId());

  // 64-bit parent ID (16 hex chars) - use current span ID as it becomes the parent for downstream
  std::string parent_id = Hex::uint64ToHex(span_context.id());

  // 8-bit trace flags (2 hex chars)
  uint8_t flags = span_context.sampled() ? 0x01 : 0x00;
  std::string trace_flags = absl::StrFormat("%02x", flags);

  return absl::StrFormat("%s-%s-%s-%s", version, trace_id, parent_id, trace_flags);
}

} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
