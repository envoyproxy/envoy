#include "source/extensions/propagators/opentelemetry/w3c/trace_context_propagator.h"

#include "source/common/common/hex.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace OpenTelemetry {
namespace {

constexpr absl::string_view kDefaultVersion = "00";

// See https://www.w3.org/TR/trace-context/#traceparent-header
constexpr int kTraceparentHeaderSize = 55; // 2 + 1 + 32 + 1 + 16 + 1 + 2
constexpr int kVersionHexSize = 2;
constexpr int kTraceIdHexSize = 32;
constexpr int kParentIdHexSize = 16;
constexpr int kTraceFlagsHexSize = 2;

bool isValidHex(const absl::string_view& input) {
  return std::all_of(input.begin(), input.end(),
                     [](const char& c) { return absl::ascii_isxdigit(c); });
}

bool isAllZeros(const absl::string_view& input) {
  return std::all_of(input.begin(), input.end(), [](const char& c) { return c == '0'; });
}

} // namespace

W3CTraceContextPropagator::W3CTraceContextPropagator()
    : trace_parent_header_("traceparent"), trace_state_header_("tracestate") {}

absl::StatusOr<SpanContext>
W3CTraceContextPropagator::extract(const Tracing::TraceContext& trace_context) {
  auto propagation_header = trace_parent_header_.get(trace_context);
  if (!propagation_header.has_value()) {
    return absl::InvalidArgumentError("No traceparent header found");
  }
  auto header_value_string = propagation_header.value();

  if (header_value_string.size() != kTraceparentHeaderSize) {
    return absl::InvalidArgumentError("Invalid traceparent header length");
  }

  // Try to split it into its component parts:
  std::vector<absl::string_view> propagation_header_components =
      absl::StrSplit(header_value_string, '-', absl::SkipEmpty());
  if (propagation_header_components.size() != 4) {
    return absl::InvalidArgumentError("Invalid traceparent hyphenation");
  }

  absl::string_view version = propagation_header_components[0];
  absl::string_view trace_id = propagation_header_components[1];
  absl::string_view span_id = propagation_header_components[2];
  absl::string_view trace_flags = propagation_header_components[3];

  if (version.size() != kVersionHexSize || trace_id.size() != kTraceIdHexSize ||
      span_id.size() != kParentIdHexSize || trace_flags.size() != kTraceFlagsHexSize) {
    return absl::InvalidArgumentError("Invalid traceparent field sizes");
  }

  if (!isValidHex(version) || !isValidHex(trace_id) || !isValidHex(span_id) ||
      !isValidHex(trace_flags)) {
    return absl::InvalidArgumentError("Invalid header hex");
  }

  // As per the traceparent header definition, if the trace-id or parent-id are all zeros, they are
  // invalid and must be ignored.
  if (isAllZeros(trace_id)) {
    return absl::InvalidArgumentError("Invalid trace id");
  }
  if (isAllZeros(span_id)) {
    return absl::InvalidArgumentError("Invalid parent id");
  }

  // Set whether or not the span is sampled from the trace flags.
  // See https://w3c.github.io/trace-context/#trace-flags.
  char decoded_trace_flags = absl::HexStringToBytes(trace_flags).front();
  bool sampled = (decoded_trace_flags & 1);

  // If a tracestate header is received without an accompanying traceparent header,
  // it is invalid and MUST be discarded. Because we're already checking for the
  // traceparent header above, we don't need to check here.
  // See https://www.w3.org/TR/trace-context/#processing-model-for-working-with-trace-context
  const auto tracestate_values = trace_state_header_.getAll(trace_context);

  SpanContext parent_context(version, trace_id, span_id, sampled,
                             absl::StrJoin(tracestate_values, ","));
  return parent_context;
}

void W3CTraceContextPropagator::inject(const SpanContext& span_context,
                                       Tracing::TraceContext& trace_context) {
  std::string trace_id_hex = span_context.traceId();
  std::string span_id_hex = span_context.spanId();
  std::vector<uint8_t> trace_flags_vec{span_context.sampled()};
  std::string trace_flags_hex = Hex::encode(trace_flags_vec);
  std::string traceparent_header_value =
      absl::StrCat(kDefaultVersion, "-", trace_id_hex, "-", span_id_hex, "-", trace_flags_hex);

  // Set the traceparent in the trace_context.
  trace_parent_header_.setRefKey(trace_context, traceparent_header_value);

  // Also set the tracestate if present.
  if (!span_context.tracestate().empty()) {
    trace_state_header_.setRefKey(trace_context, span_context.tracestate());
  }
}

std::vector<std::string> W3CTraceContextPropagator::fields() const {
  return {"traceparent", "tracestate"};
}

std::string W3CTraceContextPropagator::name() const { return "tracecontext"; }

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
