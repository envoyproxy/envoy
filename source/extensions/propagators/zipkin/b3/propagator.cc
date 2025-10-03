#include "source/extensions/propagators/zipkin/b3/propagator.h"

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
B3Propagator::extract(const Tracing::TraceContext& trace_context) {
  // Try single header format first
  auto single_result = extractSingleHeader(trace_context);
  if (single_result.ok()) {
    return single_result;
  }

  // Fall back to multi-header format
  return extractMultiHeader(trace_context);
}

void B3Propagator::inject(const Extensions::Tracers::Zipkin::SpanContext& span_context,
                          Tracing::TraceContext& trace_context) {
  // Inject both single and multi-header formats for maximum compatibility
  injectSingleHeader(span_context, trace_context);
  injectMultiHeader(span_context, trace_context);
}

std::vector<std::string> B3Propagator::fields() const {
  return {std::string(B3_SINGLE_HEADER),  std::string(B3_TRACE_ID_HEADER),
          std::string(B3_SPAN_ID_HEADER), std::string(B3_PARENT_SPAN_ID_HEADER),
          std::string(B3_SAMPLED_HEADER), std::string(B3_FLAGS_HEADER)};
}

absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
B3Propagator::extractSingleHeader(const Tracing::TraceContext& trace_context) {
  auto b3_value = trace_context.getByKey(B3_SINGLE_HEADER);
  if (!b3_value.has_value()) {
    return absl::NotFoundError("B3 single header not found");
  }

  std::vector<absl::string_view> parts = absl::StrSplit(b3_value.value(), '-');
  if (parts.empty()) {
    return absl::InvalidArgumentError("Invalid B3 single header format");
  }

  // Handle sampling-only format ("0", "1", "d")
  if (parts.size() == 1) {
    auto sampling = parseB3SamplingState(parts[0]);
    if (sampling.has_value()) {
      // Return context with sampling info but no trace data
      return Extensions::Tracers::Zipkin::SpanContext(0, 0, 0, 0, sampling.value());
    }
    return absl::InvalidArgumentError("Invalid B3 sampling-only header");
  }

  if (parts.size() < 2 || parts.size() > 4) {
    return absl::InvalidArgumentError("Invalid B3 single header format");
  }

  // Parse trace ID (parts[0])
  uint64_t trace_id_high = 0, trace_id_low = 0;
  if (!Extensions::Tracers::Common::Utils::Trace::parseTraceId(std::string(parts[0]), trace_id_high,
                                                               trace_id_low)) {
    return absl::InvalidArgumentError("Invalid trace ID in B3 header");
  }

  // Parse span ID (parts[1])
  uint64_t span_id = 0;
  if (!Extensions::Tracers::Common::Utils::Trace::parseSpanId(std::string(parts[1]), span_id)) {
    return absl::InvalidArgumentError("Invalid span ID in B3 header");
  }

  // Parse sampling state (parts[2] if present)
  bool sampled = false;
  if (parts.size() > 2) {
    auto sampling = parseB3SamplingState(parts[2]);
    if (!sampling.has_value()) {
      return absl::InvalidArgumentError("Invalid sampling state in B3 header");
    }
    sampled = sampling.value();
  }

  // Parse parent span ID (parts[3] if present)
  uint64_t parent_span_id = 0;
  if (parts.size() > 3) {
    if (!Extensions::Tracers::Common::Utils::Trace::parseSpanId(std::string(parts[3]),
                                                                parent_span_id)) {
      return absl::InvalidArgumentError("Invalid parent span ID in B3 header");
    }
  }

  return Extensions::Tracers::Zipkin::SpanContext(trace_id_high, trace_id_low, span_id,
                                                  parent_span_id, sampled);
}

absl::StatusOr<Extensions::Tracers::Zipkin::SpanContext>
B3Propagator::extractMultiHeader(const Tracing::TraceContext& trace_context) {
  auto trace_id_header = trace_context.getByKey(B3_TRACE_ID_HEADER);
  auto span_id_header = trace_context.getByKey(B3_SPAN_ID_HEADER);

  if (!trace_id_header.has_value() || !span_id_header.has_value()) {
    return absl::NotFoundError("Required B3 headers not found");
  }

  // Parse trace ID
  uint64_t trace_id_high = 0, trace_id_low = 0;
  if (!Extensions::Tracers::Common::Utils::Trace::parseTraceId(trace_id_header.value(),
                                                               trace_id_high, trace_id_low)) {
    return absl::InvalidArgumentError("Invalid trace ID in B3 headers");
  }

  // Parse span ID
  uint64_t span_id = 0;
  if (!Extensions::Tracers::Common::Utils::Trace::parseSpanId(span_id_header.value(), span_id)) {
    return absl::InvalidArgumentError("Invalid span ID in B3 headers");
  }

  // Parse parent span ID (optional)
  uint64_t parent_span_id = 0;
  auto parent_span_id_header = trace_context.getByKey(B3_PARENT_SPAN_ID_HEADER);
  if (parent_span_id_header.has_value()) {
    if (!Extensions::Tracers::Common::Utils::Trace::parseSpanId(parent_span_id_header.value(),
                                                                parent_span_id)) {
      return absl::InvalidArgumentError("Invalid parent span ID in B3 headers");
    }
  }

  // Parse sampling state
  bool sampled = false;
  auto sampled_header = trace_context.getByKey(B3_SAMPLED_HEADER);
  if (sampled_header.has_value()) {
    auto sampling = parseB3SamplingState(sampled_header.value());
    if (!sampling.has_value()) {
      return absl::InvalidArgumentError("Invalid sampling state in B3 headers");
    }
    sampled = sampling.value();
  }

  // Check for debug flag
  auto flags_header = trace_context.getByKey(B3_FLAGS_HEADER);
  if (flags_header.has_value() && flags_header.value() == "1") {
    sampled = true; // Debug flag implies sampling
  }

  return Extensions::Tracers::Zipkin::SpanContext(trace_id_high, trace_id_low, span_id,
                                                  parent_span_id, sampled);
}

void B3Propagator::injectSingleHeader(const Extensions::Tracers::Zipkin::SpanContext& span_context,
                                      Tracing::TraceContext& trace_context) {
  std::string b3_value = Hex::uint64ToHex(span_context.traceIdHigh()) +
                         Hex::uint64ToHex(span_context.traceId()) + "-" +
                         Hex::uint64ToHex(span_context.id());

  if (span_context.sampled()) {
    b3_value += "-1";
  } else {
    b3_value += "-0";
  }

  if (span_context.parentId() != 0) {
    b3_value += "-" + Hex::uint64ToHex(span_context.parentId());
  }

  trace_context.setByKey(B3_SINGLE_HEADER, b3_value);
}

void B3Propagator::injectMultiHeader(const Extensions::Tracers::Zipkin::SpanContext& span_context,
                                     Tracing::TraceContext& trace_context) {
  // Inject trace ID (full 128-bit)
  std::string trace_id =
      Hex::uint64ToHex(span_context.traceIdHigh()) + Hex::uint64ToHex(span_context.traceId());
  trace_context.setByKey(B3_TRACE_ID_HEADER, trace_id);

  // Inject span ID
  trace_context.setByKey(B3_SPAN_ID_HEADER, Hex::uint64ToHex(span_context.id()));

  // Inject parent span ID if present
  if (span_context.parentId() != 0) {
    trace_context.setByKey(B3_PARENT_SPAN_ID_HEADER, Hex::uint64ToHex(span_context.parentId()));
  }

  // Inject sampling state
  trace_context.setByKey(B3_SAMPLED_HEADER, span_context.sampled() ? "1" : "0");
}

absl::optional<bool> B3Propagator::parseB3SamplingState(absl::string_view sampling_state) {
  if (sampling_state == "0") {
    return false;
  } else if (sampling_state == "1" || sampling_state == "d") {
    return true;
  }
  return absl::nullopt;
}

} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
