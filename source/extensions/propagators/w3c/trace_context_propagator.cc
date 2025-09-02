#include "source/extensions/propagators/w3c/trace_context_propagator.h"

#include "absl/strings/string_view.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/numbers.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {

TraceContextPropagator::TraceContextPropagator()
    : traceparent_header_("traceparent"), tracestate_header_("tracestate") {}

absl::StatusOr<SpanContext>
TraceContextPropagator::extract(const Tracing::TraceContext& trace_context) {
  auto traceparent_value = trace_context.getByKey(traceparent_header_.key());
  if (!traceparent_value.has_value()) {
    return absl::NotFoundError("traceparent header not present");
  }

  auto tracestate_value = trace_context.getByKey(tracestate_header_.key());
  std::string tracestate = tracestate_value.has_value() ? tracestate_value.value() : "";

  return parseTraceparent(traceparent_value.value(), tracestate);
}

void TraceContextPropagator::inject(const SpanContext& span_context,
                                    Tracing::TraceContext& trace_context) {
  if (!span_context.isValid()) {
    return;
  }

  std::string traceparent = formatTraceparent(span_context);
  trace_context.setByKey(traceparent_header_.key(), traceparent);

  if (!span_context.tracestate().empty()) {
    trace_context.setByKey(tracestate_header_.key(), span_context.tracestate());
  }
}

std::vector<std::string> TraceContextPropagator::fields() const {
  return {"traceparent", "tracestate"};
}

std::string TraceContextPropagator::name() const { return "tracecontext"; }

absl::StatusOr<SpanContext>
TraceContextPropagator::parseTraceparent(const std::string& traceparent_value,
                                         const std::string& tracestate_value) {
  std::vector<std::string> parts = absl::StrSplit(traceparent_value, '-');

  if (parts.size() != 4) {
    return absl::InvalidArgumentError("Invalid traceparent format: expected 4 parts");
  }

  // Parse version
  const std::string& version = parts[0];
  if (version.length() != 2) {
    return absl::InvalidArgumentError("Invalid traceparent version format");
  }

  // Parse trace ID (32 hex characters)
  const std::string& trace_id_str = parts[1];
  if (trace_id_str.length() != 32) {
    return absl::InvalidArgumentError("Invalid trace ID length in traceparent");
  }

  TraceId trace_id(trace_id_str);
  if (!trace_id.isValid()) {
    return absl::InvalidArgumentError("Invalid trace ID in traceparent");
  }

  // Parse parent/span ID (16 hex characters)
  const std::string& span_id_str = parts[2];
  if (span_id_str.length() != 16) {
    return absl::InvalidArgumentError("Invalid parent ID length in traceparent");
  }

  SpanId span_id(span_id_str);
  if (!span_id.isValid()) {
    return absl::InvalidArgumentError("Invalid parent ID in traceparent");
  }

  // Parse trace flags (2 hex characters)
  const std::string& flags_str = parts[3];
  if (flags_str.length() != 2) {
    return absl::InvalidArgumentError("Invalid trace flags length in traceparent");
  }

  uint64_t flags_value;
  if (!absl::SimpleHexAtoi(flags_str, &flags_value)) {
    return absl::InvalidArgumentError("Invalid trace flags format in traceparent");
  }

  TraceFlags trace_flags(static_cast<uint8_t>(flags_value));

  return SpanContext(std::move(trace_id), std::move(span_id), trace_flags, absl::nullopt,
                     tracestate_value);
}

std::string TraceContextPropagator::formatTraceparent(const SpanContext& span_context) {
  return absl::StrCat("00-", span_context.traceId().toHex(), "-", span_context.spanId().toHex(),
                      "-", absl::Hex(span_context.traceFlags().value(), absl::kZeroPad2));
}

} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
