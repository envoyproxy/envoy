#include "source/extensions/propagators/b3/propagator.h"

#include "absl/strings/string_view.h"
#include "absl/strings/str_split.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace B3 {

B3Propagator::B3Propagator()
    : b3_header_("b3"), x_b3_trace_id_header_("x-b3-traceid"), x_b3_span_id_header_("x-b3-spanid"),
      x_b3_sampled_header_("x-b3-sampled"), x_b3_flags_header_("x-b3-flags"),
      x_b3_parent_span_id_header_("x-b3-parentspanid") {}

absl::StatusOr<SpanContext> B3Propagator::extract(const Tracing::TraceContext& trace_context) {
  // Try single header format first
  auto single_result = extractSingleHeader(trace_context);
  if (single_result.ok()) {
    return single_result;
  }

  // Try multi-header format
  auto multi_result = extractMultiHeader(trace_context);
  if (multi_result.ok()) {
    return multi_result;
  }

  return absl::NotFoundError("No valid B3 headers found");
}

void B3Propagator::inject(const SpanContext& span_context, Tracing::TraceContext& trace_context) {
  if (!span_context.isValid()) {
    return;
  }

  // Inject both single and multi-header formats for maximum compatibility
  injectSingleHeader(span_context, trace_context);
  injectMultiHeader(span_context, trace_context);
}

std::vector<std::string> B3Propagator::fields() const {
  return {"b3", "x-b3-traceid", "x-b3-spanid", "x-b3-sampled", "x-b3-flags", "x-b3-parentspanid"};
}

std::string B3Propagator::name() const { return "b3"; }

absl::StatusOr<SpanContext>
B3Propagator::extractSingleHeader(const Tracing::TraceContext& trace_context) {
  auto b3_value = trace_context.getByKey(b3_header_.key());
  if (!b3_value.has_value()) {
    return absl::NotFoundError("B3 single header not present");
  }

  std::vector<std::string> parts = absl::StrSplit(b3_value.value(), '-');

  if (parts.size() < 2) {
    // Handle sampling-only headers like "0", "1", "d"
    if (parts.size() == 1) {
      auto sampling = parseB3SamplingState(parts[0]);
      if (sampling.has_value()) {
        return absl::InvalidArgumentError(
            "B3 single header contains only sampling state, no trace context");
      }
    }
    return absl::InvalidArgumentError("Invalid B3 single header format");
  }

  // Parse trace ID (required)
  TraceId trace_id(parts[0]);
  if (!trace_id.isValid()) {
    return absl::InvalidArgumentError("Invalid trace ID in B3 single header");
  }

  // Parse span ID (required)
  SpanId span_id(parts[1]);
  if (!span_id.isValid()) {
    return absl::InvalidArgumentError("Invalid span ID in B3 single header");
  }

  // Parse sampling state (optional, defaults to not sampled)
  TraceFlags trace_flags;
  if (parts.size() > 2 && !parts[2].empty()) {
    auto sampling = parseB3SamplingState(parts[2]);
    if (sampling.has_value()) {
      trace_flags.setSampled(sampling.value());
    }
  }

  // Parse parent span ID (optional)
  absl::optional<SpanId> parent_span_id;
  if (parts.size() > 3 && !parts[3].empty()) {
    SpanId parent_id(parts[3]);
    if (parent_id.isValid()) {
      parent_span_id = parent_id;
    }
  }

  return SpanContext(std::move(trace_id), std::move(span_id), trace_flags, parent_span_id);
}

absl::StatusOr<SpanContext>
B3Propagator::extractMultiHeader(const Tracing::TraceContext& trace_context) {
  auto trace_id_value = trace_context.getByKey(x_b3_trace_id_header_.key());
  auto span_id_value = trace_context.getByKey(x_b3_span_id_header_.key());

  if (!trace_id_value.has_value() || !span_id_value.has_value()) {
    // Check for sampling-only headers
    auto sampled_value = trace_context.getByKey(x_b3_sampled_header_.key());
    auto flags_value = trace_context.getByKey(x_b3_flags_header_.key());

    if (sampled_value.has_value() || flags_value.has_value()) {
      return absl::InvalidArgumentError(
          "B3 multi-header contains only sampling state, no trace context");
    }

    return absl::NotFoundError("Required B3 multi-header fields not present");
  }

  // Parse trace ID (required)
  TraceId trace_id(trace_id_value.value());
  if (!trace_id.isValid()) {
    return absl::InvalidArgumentError("Invalid trace ID in B3 multi-header");
  }

  // Parse span ID (required)
  SpanId span_id(span_id_value.value());
  if (!span_id.isValid()) {
    return absl::InvalidArgumentError("Invalid span ID in B3 multi-header");
  }

  // Parse sampling state
  TraceFlags trace_flags;
  auto sampled_value = trace_context.getByKey(x_b3_sampled_header_.key());
  auto flags_value = trace_context.getByKey(x_b3_flags_header_.key());

  // Check flags first (debug flag has priority)
  if (flags_value.has_value()) {
    auto sampling = parseB3SamplingState(flags_value.value());
    if (sampling.has_value()) {
      trace_flags.setSampled(sampling.value());
    }
  } else if (sampled_value.has_value()) {
    auto sampling = parseB3SamplingState(sampled_value.value());
    if (sampling.has_value()) {
      trace_flags.setSampled(sampling.value());
    }
  }

  // Parse parent span ID (optional)
  absl::optional<SpanId> parent_span_id;
  auto parent_span_id_value = trace_context.getByKey(x_b3_parent_span_id_header_.key());
  if (parent_span_id_value.has_value()) {
    SpanId parent_id(parent_span_id_value.value());
    if (parent_id.isValid()) {
      parent_span_id = parent_id;
    }
  }

  return SpanContext(std::move(trace_id), std::move(span_id), trace_flags, parent_span_id);
}

void B3Propagator::injectSingleHeader(const SpanContext& span_context,
                                      Tracing::TraceContext& trace_context) {
  std::string b3_value = span_context.traceId().toHex() + "-" + span_context.spanId().toHex();

  // Add sampling state
  if (span_context.sampled()) {
    b3_value += "-1";
  } else {
    b3_value += "-0";
  }

  // Add parent span ID if present
  if (span_context.parentSpanId().has_value()) {
    b3_value += "-" + span_context.parentSpanId()->toHex();
  }

  trace_context.setByKey(b3_header_.key(), b3_value);
}

void B3Propagator::injectMultiHeader(const SpanContext& span_context,
                                     Tracing::TraceContext& trace_context) {
  trace_context.setByKey(x_b3_trace_id_header_.key(), span_context.traceId().toHex());
  trace_context.setByKey(x_b3_span_id_header_.key(), span_context.spanId().toHex());

  // Set sampling state
  if (span_context.sampled()) {
    trace_context.setByKey(x_b3_sampled_header_.key(), "1");
  } else {
    trace_context.setByKey(x_b3_sampled_header_.key(), "0");
  }

  // Set parent span ID if present
  if (span_context.parentSpanId().has_value()) {
    trace_context.setByKey(x_b3_parent_span_id_header_.key(), span_context.parentSpanId()->toHex());
  }
}

} // namespace B3
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
