#include "span_context_extractor.h"

#include "envoy/common/exception.h"
#include "envoy/tracing/http_tracer.h"

#include "source/common/http/header_map_impl.h"

#include "absl/strings/escaping.h"
#include "span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

const Http::LowerCaseString& openTelemetryPropagationHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "traceparent");
}

// See https://www.w3.org/TR/trace-context/#traceparent-header
constexpr int kTraceparentHeaderSize = 55; // 2 + 1 + 32 + 1 + 16 + 1 + 2
constexpr int kVersionHexSize = 2;
constexpr int kTraceIdHexSize = 32;
constexpr int kParentIdHexSize = 16;
constexpr int kTraceFlagsHexSize = 2;

bool isValidHex(const std::string& input) {
  return std::all_of(input.begin(), input.end(),
                     [](const char& c) { return absl::ascii_isxdigit(c); });
}

bool isAllZeros(const std::string& input) {
  return std::all_of(input.begin(), input.end(), [](const char& c) { return c == '0'; });
}

} // namespace

SpanContextExtractor::SpanContextExtractor(Tracing::TraceContext& trace_context)
    : trace_context_(trace_context) {}

SpanContextExtractor::~SpanContextExtractor() = default;

bool SpanContextExtractor::propagationHeaderPresent() {
  auto propagation_header = trace_context_.getByKey(openTelemetryPropagationHeader());
  return propagation_header.has_value();
}

SpanContext SpanContextExtractor::extractSpanContext() {
  auto propagation_header = trace_context_.getByKey(openTelemetryPropagationHeader());
  if (!propagation_header.has_value()) {
    // We should have already caught this, but just in case.
    throw ExtractorException("No propagation header found");
  }
  auto header_value_string = propagation_header.value();

  if (header_value_string.size() != kTraceparentHeaderSize) {
    throw ExtractorException("Invalid traceparent header length");
  }
  // Try to split it into its component parts:
  std::vector<std::string> propagation_header_components =
      absl::StrSplit(header_value_string, '-', absl::SkipEmpty());
  if (propagation_header_components.size() != 4) {
    throw ExtractorException("Invalid traceparent hyphenation");
  }
  std::string version = propagation_header_components[0];
  std::string trace_id = propagation_header_components[1];
  std::string parent_id = propagation_header_components[2];
  std::string trace_flags = propagation_header_components[3];
  if (version.size() != kVersionHexSize || trace_id.size() != kTraceIdHexSize ||
      parent_id.size() != kParentIdHexSize || trace_flags.size() != kTraceFlagsHexSize) {
    throw ExtractorException("Invalid traceparent field sizes");
  }
  if (!isValidHex(version) || !isValidHex(trace_id) || !isValidHex(parent_id) ||
      !isValidHex(trace_flags)) {
    throw ExtractorException("Invalid header hex");
  }
  // As per the traceparent header definition, if the trace-id or parent-id are all zeros, they are
  // invalid and must be ignored.
  if (isAllZeros(trace_id)) {
    throw ExtractorException("Invalid trace id");
  }
  if (isAllZeros(parent_id)) {
    throw ExtractorException("Invalid parent id");
  }

  // Set whether or not the span is sampled from the trace flags.
  // See https://w3c.github.io/trace-context/#trace-flags.
  char decoded_trace_flags = absl::HexStringToBytes(trace_flags).front();
  bool sampled = (decoded_trace_flags & 1);
  SpanContext span_context(version, trace_id, parent_id, sampled);
  return span_context;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
