#include "source/extensions/propagators/opentelemetry/b3/propagator.h"

#include "source/common/tracing/trace_context_impl.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

constexpr absl::string_view kDefaultVersion = "00";

bool isValidB3TraceId(const absl::string_view& trace_id) {
  // B3 trace IDs can be 16 or 32 hex characters
  return (trace_id.size() == 16 || trace_id.size() == 32) &&
         std::all_of(trace_id.begin(), trace_id.end(),
                     [](const char& c) { return absl::ascii_isxdigit(c); }) &&
         !std::all_of(trace_id.begin(), trace_id.end(), [](const char& c) { return c == '0'; });
}

bool isValidB3SpanId(const absl::string_view& span_id) {
  // B3 span IDs must be 16 hex characters
  return span_id.size() == 16 &&
         std::all_of(span_id.begin(), span_id.end(),
                     [](const char& c) { return absl::ascii_isxdigit(c); }) &&
         !std::all_of(span_id.begin(), span_id.end(), [](const char& c) { return c == '0'; });
}

std::string normalizeB3TraceId(const absl::string_view& trace_id) {
  // Convert 16-char trace ID to 32-char by zero-padding
  if (trace_id.size() == 16) {
    return absl::StrCat(std::string(16, '0'), trace_id);
  }
  return std::string(trace_id);
}

bool parseB3Sampled(const absl::string_view& sampled_str) {
  if (sampled_str == "1" || sampled_str == "true") {
    return true;
  }
  return false;
}

} // namespace

B3Propagator::B3Propagator()
    : b3_header_("b3"), x_b3_trace_id_header_("X-B3-TraceId"), x_b3_span_id_header_("X-B3-SpanId"),
      x_b3_sampled_header_("X-B3-Sampled"), x_b3_flags_header_("X-B3-Flags"),
      x_b3_parent_span_id_header_("X-B3-ParentSpanId") {}

absl::StatusOr<SpanContext> B3Propagator::extract(const Tracing::TraceContext& trace_context) {
  // Try single header format first
  auto single_result = extractSingleHeader(trace_context);
  if (single_result.ok()) {
    return single_result;
  }

  // Fall back to multi-header format
  return extractMultiHeader(trace_context);
}

absl::StatusOr<SpanContext>
B3Propagator::extractSingleHeader(const Tracing::TraceContext& trace_context) {
  auto b3_header = b3_header_.get(trace_context);
  if (!b3_header.has_value()) {
    return absl::InvalidArgumentError("No b3 header found");
  }

  auto header_value = b3_header.value();

  // Handle special cases
  if (header_value == "0") {
    return absl::InvalidArgumentError("B3 not sampled");
  }
  if (header_value == "1") {
    return absl::InvalidArgumentError("B3 debug flag without trace context");
  }

  // Parse format: {TraceId}-{SpanId}-{SamplingState}-{ParentSpanId}
  // SamplingState and ParentSpanId are optional
  std::vector<absl::string_view> parts = absl::StrSplit(header_value, '-');
  if (parts.size() < 2 || parts.size() > 4) {
    return absl::InvalidArgumentError("Invalid B3 header format");
  }

  absl::string_view trace_id = parts[0];
  absl::string_view span_id = parts[1];

  if (!isValidB3TraceId(trace_id) || !isValidB3SpanId(span_id)) {
    return absl::InvalidArgumentError("Invalid B3 trace or span ID");
  }

  bool sampled = false;
  if (parts.size() >= 3 && !parts[2].empty()) {
    sampled = parseB3Sampled(parts[2]);
  }

  std::string normalized_trace_id = normalizeB3TraceId(trace_id);
  SpanContext span_context(kDefaultVersion, normalized_trace_id, span_id, sampled, "");
  return span_context;
}

absl::StatusOr<SpanContext>
B3Propagator::extractMultiHeader(const Tracing::TraceContext& trace_context) {
  auto trace_id_header = x_b3_trace_id_header_.get(trace_context);
  auto span_id_header = x_b3_span_id_header_.get(trace_context);

  if (!trace_id_header.has_value() || !span_id_header.has_value()) {
    return absl::InvalidArgumentError("Missing required B3 multi-headers");
  }

  auto trace_id = trace_id_header.value();
  auto span_id = span_id_header.value();

  if (!isValidB3TraceId(trace_id) || !isValidB3SpanId(span_id)) {
    return absl::InvalidArgumentError("Invalid B3 trace or span ID");
  }

  bool sampled = false;
  auto sampled_header = x_b3_sampled_header_.get(trace_context);
  if (sampled_header.has_value()) {
    sampled = parseB3Sampled(sampled_header.value());
  }

  // Check for debug flag
  auto flags_header = x_b3_flags_header_.get(trace_context);
  if (flags_header.has_value() && flags_header.value() == "1") {
    sampled = true; // Debug flag implies sampling
  }

  std::string normalized_trace_id = normalizeB3TraceId(trace_id);
  SpanContext span_context(kDefaultVersion, normalized_trace_id, span_id, sampled, "");
  return span_context;
}

void B3Propagator::inject(const SpanContext& span_context, Tracing::TraceContext& trace_context) {
  const std::string& trace_id = span_context.traceId();
  const std::string& span_id = span_context.spanId();
  bool sampled = span_context.sampled();

  // Inject single header format
  std::string sampled_str = sampled ? "1" : "0";
  std::string b3_value = absl::StrCat(trace_id, "-", span_id, "-", sampled_str);
  b3_header_.setRefKey(trace_context, b3_value);

  // Inject multi-header format
  x_b3_trace_id_header_.setRefKey(trace_context, trace_id);
  x_b3_span_id_header_.setRefKey(trace_context, span_id);
  x_b3_sampled_header_.setRefKey(trace_context, sampled_str);
}

std::vector<std::string> B3Propagator::fields() const {
  return {"b3", "X-B3-TraceId", "X-B3-SpanId", "X-B3-Sampled", "X-B3-Flags", "X-B3-ParentSpanId"};
}

std::string B3Propagator::name() const { return "b3"; }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
