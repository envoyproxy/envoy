#include "source/extensions/tracers/opentelemetry/b3_span_context.h"

#include <algorithm>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

constexpr size_t kTraceId64HexSize = 16;
constexpr size_t kTraceId128HexSize = 32;
constexpr size_t kSpanIdHexSize = 16;

bool isValidHex(absl::string_view input) {
  return std::all_of(input.begin(), input.end(),
                     [](const char& c) { return absl::ascii_isxdigit(c); });
}

bool isAllZeros(absl::string_view input) {
  return std::all_of(input.begin(), input.end(), [](const char& c) { return c == '0'; });
}

bool isValidTraceId(absl::string_view trace_id) {
  return (trace_id.size() == kTraceId64HexSize || trace_id.size() == kTraceId128HexSize) &&
         isValidHex(trace_id) && !isAllZeros(trace_id);
}

bool isValidSpanId(absl::string_view span_id) {
  return span_id.size() == kSpanIdHexSize && isValidHex(span_id) && !isAllZeros(span_id);
}

std::string toTraceId128(absl::string_view trace_id) {
  if (trace_id.size() == kTraceId128HexSize) {
    return std::string(trace_id);
  }
  return absl::StrCat(std::string(kTraceId64HexSize, '0'), trace_id);
}

// A single header of one character carries only a sampling decision, e.g. "b3: 0".
bool isSamplingOnly(absl::string_view b3) { return b3.size() == 1; }

bool multipleHeadersPresent(const Tracing::TraceContext& trace_context) {
  return B3Constants::get().X_B3_TRACE_ID.get(trace_context).has_value() ||
         B3Constants::get().X_B3_SPAN_ID.get(trace_context).has_value();
}

// b3: {TraceId}-{SpanId}-{SamplingState}-{ParentSpanId}, where the last two fields are optional.
absl::StatusOr<B3SpanContext> extractFromSingleHeader(absl::string_view b3) {
  const std::vector<absl::string_view> fields = absl::StrSplit(b3, '-');
  if (fields.size() < 2 || fields.size() > 4) {
    return absl::InvalidArgumentError("Invalid b3 header field count");
  }
  if (!isValidTraceId(fields[0])) {
    return absl::InvalidArgumentError("Invalid b3 trace id");
  }
  if (!isValidSpanId(fields[1])) {
    return absl::InvalidArgumentError("Invalid b3 span id");
  }

  B3SpanContext span_context{toTraceId128(fields[0]), std::string(fields[1]), std::nullopt};
  if (fields.size() >= 3) {
    if (fields[2] == "1" || fields[2] == "d") {
      span_context.sampled = true;
    } else if (fields[2] == "0") {
      span_context.sampled = false;
    } else {
      return absl::InvalidArgumentError("Invalid b3 sampling state");
    }
  }
  if (fields.size() == 4 && (fields[3].size() != kSpanIdHexSize || !isValidHex(fields[3]))) {
    return absl::InvalidArgumentError("Invalid b3 parent span id");
  }
  return span_context;
}

absl::StatusOr<B3SpanContext>
extractFromMultipleHeaders(const Tracing::TraceContext& trace_context) {
  const auto trace_id = B3Constants::get().X_B3_TRACE_ID.get(trace_context);
  const auto span_id = B3Constants::get().X_B3_SPAN_ID.get(trace_context);
  if (!trace_id.has_value() || !span_id.has_value()) {
    return absl::InvalidArgumentError("Missing x-b3-traceid or x-b3-spanid header");
  }
  if (!isValidTraceId(trace_id.value())) {
    return absl::InvalidArgumentError("Invalid x-b3-traceid header");
  }
  if (!isValidSpanId(span_id.value())) {
    return absl::InvalidArgumentError("Invalid x-b3-spanid header");
  }

  B3SpanContext span_context{toTraceId128(trace_id.value()), std::string(span_id.value()),
                             std::nullopt};
  // The debug flag implies an accept sampling decision.
  const auto flags = B3Constants::get().X_B3_FLAGS.get(trace_context);
  if (flags.has_value() && flags.value() == "1") {
    span_context.sampled = true;
    return span_context;
  }
  // "true" is accepted for compatibility with older Zipkin tracers.
  const auto sampled = B3Constants::get().X_B3_SAMPLED.get(trace_context);
  if (sampled.has_value()) {
    span_context.sampled = sampled.value() == "1" || sampled.value() == "true";
  }
  return span_context;
}

} // namespace

bool b3HeadersPresent(const Tracing::TraceContext& trace_context) {
  const auto b3 = B3Constants::get().B3.get(trace_context);
  if (b3.has_value() && !isSamplingOnly(b3.value())) {
    return true;
  }
  return multipleHeadersPresent(trace_context);
}

absl::StatusOr<B3SpanContext> extractB3SpanContext(const Tracing::TraceContext& trace_context) {
  const auto b3 = B3Constants::get().B3.get(trace_context);
  if (b3.has_value() && !isSamplingOnly(b3.value())) {
    absl::StatusOr<B3SpanContext> span_context = extractFromSingleHeader(b3.value());
    if (span_context.ok() || !multipleHeadersPresent(trace_context)) {
      return span_context;
    }
  }
  return extractFromMultipleHeaders(trace_context);
}

void injectB3SingleHeader(Tracing::TraceContext& trace_context, absl::string_view trace_id,
                          absl::string_view span_id, bool sampled) {
  B3Constants::get().B3.setRefKey(trace_context,
                                  absl::StrCat(trace_id, "-", span_id, "-", sampled ? "1" : "0"));
}

void injectB3MultiHeaders(Tracing::TraceContext& trace_context, absl::string_view trace_id,
                          absl::string_view span_id, bool sampled) {
  B3Constants::get().X_B3_TRACE_ID.setRefKey(trace_context, trace_id);
  B3Constants::get().X_B3_SPAN_ID.setRefKey(trace_context, span_id);
  B3Constants::get().X_B3_SAMPLED.setRefKey(trace_context, sampled ? "1" : "0");
  B3Constants::get().X_B3_PARENT_SPAN_ID.remove(trace_context);
  B3Constants::get().X_B3_FLAGS.remove(trace_context);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
