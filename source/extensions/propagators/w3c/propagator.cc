#include "source/extensions/propagators/w3c/propagator.h"

#include <map>

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {

namespace {

// Constants for improved maintainability
constexpr char kTraceparentNotFoundMessage[] = "No traceparent header found";

/**
 * Extracts traceparent header and validates its presence.
 */
absl::StatusOr<TraceParent> extractTraceParent(const Tracing::TraceContext& trace_context) {
  auto traceparent_header = W3CConstants::get().TRACE_PARENT.get(trace_context);
  if (!traceparent_header.has_value()) {
    return absl::InvalidArgumentError(kTraceparentNotFoundMessage);
  }

  return TraceParent::parse(traceparent_header.value());
}

/**
 * Extracts tracestate header with proper handling of multiple values.
 */
TraceState extractTraceStateOptional(const Tracing::TraceContext& trace_context) {
  auto tracestate_headers = W3CConstants::get().TRACE_STATE.getAll(trace_context);
  if (tracestate_headers.empty()) {
    return TraceState();
  }

  // Join multiple tracestate headers with comma as per W3C spec
  std::string combined_tracestate = absl::StrJoin(tracestate_headers, ",");
  auto tracestate_result = TraceState::parse(combined_tracestate);

  // Return parsed tracestate or empty one if parsing fails (optional header)
  return tracestate_result.ok() ? std::move(tracestate_result.value()) : TraceState();
}

/**
 * Extracts baggage header with proper error handling.
 */
Baggage extractBaggageOptional(const Tracing::TraceContext& trace_context) {
  auto baggage_result = Propagator::extractBaggage(trace_context);
  // Return parsed baggage or empty one if parsing fails (optional header)
  return baggage_result.ok() ? std::move(baggage_result.value()) : Baggage();
}

/**
 * Injects traceparent header (always required).
 */
void injectTraceparent(const TraceContext& w3c_context, Tracing::TraceContext& trace_context) {
  std::string traceparent_value = w3c_context.traceParent().toString();
  W3CConstants::get().TRACE_PARENT.set(trace_context, traceparent_value);
}

/**
 * Injects tracestate header if present.
 */
void injectTracestateIfPresent(const TraceContext& w3c_context,
                               Tracing::TraceContext& trace_context) {
  if (!w3c_context.hasTraceState()) {
    return;
  }

  std::string tracestate_value = w3c_context.traceState().toString();
  if (!tracestate_value.empty()) {
    W3CConstants::get().TRACE_STATE.set(trace_context, tracestate_value);
  }
}

/**
 * Injects baggage header if present.
 */
void injectBaggageIfPresent(const TraceContext& w3c_context, Tracing::TraceContext& trace_context) {
  if (w3c_context.hasBaggage()) {
    Propagator::injectBaggage(w3c_context.baggage(), trace_context);
  }
}

/**
 * Validates span ID for child creation.
 */
absl::Status validateSpanIdForChild(absl::string_view new_span_id) {
  if (!Propagator::isValidHexString(new_span_id, Constants::kParentIdSize)) {
    return absl::InvalidArgumentError("Invalid span ID: must be 16 hex characters");
  }
  return absl::OkStatus();
}

/**
 * Validates trace ID and span ID for root creation.
 */
absl::Status validateIdsForRoot(absl::string_view trace_id, absl::string_view span_id) {
  if (!Propagator::isValidHexString(trace_id, Constants::kTraceIdSize)) {
    return absl::InvalidArgumentError("Invalid trace ID: must be 32 hex characters");
  }
  if (!Propagator::isValidHexString(span_id, Constants::kParentIdSize)) {
    return absl::InvalidArgumentError("Invalid span ID: must be 16 hex characters");
  }
  return absl::OkStatus();
}

/**
 * Creates child traceparent with updated parent-id.
 */
TraceParent createChildTraceparent(const TraceContext& parent_context,
                                   absl::string_view new_span_id) {
  return TraceParent(parent_context.traceParent().version(), parent_context.traceParent().traceId(),
                     std::string(new_span_id), parent_context.traceParent().traceFlags());
}

/**
 * Creates root traceparent with sampling flags.
 */
TraceParent createRootTraceparent(absl::string_view trace_id, absl::string_view span_id,
                                  bool sampled) {
  std::string trace_flags = sampled ? "01" : "00";
  return TraceParent(std::string(Constants::kCurrentVersion), std::string(trace_id),
                     std::string(span_id), trace_flags);
}

/**
 * Extracts a specific baggage value by key.
 */
std::string extractBaggageValue(const Tracing::TraceContext& trace_context, absl::string_view key) {
  auto baggage_result = Propagator::extractBaggage(trace_context);
  if (!baggage_result.ok()) {
    return "";
  }

  auto value = baggage_result.value().get(key);
  return value.has_value() ? std::string(value.value()) : "";
}

/**
 * Updates baggage with new key-value pair.
 */
bool updateBaggageWithKeyValue(Tracing::TraceContext& trace_context, absl::string_view key,
                               absl::string_view value) {
  // Extract existing baggage or create new one
  auto baggage_result = Propagator::extractBaggage(trace_context);
  Baggage baggage = baggage_result.ok() ? std::move(baggage_result.value()) : Baggage();

  // Set the new value with size limit validation
  if (!baggage.set(key, value)) {
    return false; // Size limits exceeded
  }

  // Inject updated baggage back
  Propagator::injectBaggage(baggage, trace_context);
  return true;
}

/**
 * Extracts all baggage as a map for convenience.
 */
std::map<std::string, std::string>
extractAllBaggageAsMap(const Tracing::TraceContext& trace_context) {
  std::map<std::string, std::string> result;

  auto baggage_result = Propagator::extractBaggage(trace_context);
  if (!baggage_result.ok()) {
    return result;
  }

  const auto& members = baggage_result.value().getMembers();
  for (const auto& member : members) {
    result[member.key()] = member.value();
  }

  return result;
}

} // namespace

bool Propagator::isPresent(const Tracing::TraceContext& trace_context) {
  return W3CConstants::get().TRACE_PARENT.get(trace_context).has_value();
}

absl::StatusOr<TraceContext> Propagator::extract(const Tracing::TraceContext& trace_context) {
  // Extract required traceparent header
  auto traceparent_result = extractTraceParent(trace_context);
  if (!traceparent_result.ok()) {
    return traceparent_result.status();
  }

  // Extract optional headers (don't fail on errors as they're optional)
  TraceState tracestate = extractTraceStateOptional(trace_context);
  Baggage baggage = extractBaggageOptional(trace_context);

  return TraceContext(std::move(traceparent_result.value()), std::move(tracestate),
                      std::move(baggage));
}

void Propagator::inject(const TraceContext& w3c_context, Tracing::TraceContext& trace_context) {
  injectTraceparent(w3c_context, trace_context);
  injectTracestateIfPresent(w3c_context, trace_context);
  injectBaggageIfPresent(w3c_context, trace_context);
}

absl::StatusOr<TraceContext> Propagator::createChild(const TraceContext& parent_context,
                                                     absl::string_view new_span_id) {
  auto validation_result = validateSpanIdForChild(new_span_id);
  if (!validation_result.ok()) {
    return validation_result;
  }

  TraceParent child_traceparent = createChildTraceparent(parent_context, new_span_id);
  return TraceContext(std::move(child_traceparent), parent_context.traceState());
}

absl::StatusOr<TraceContext> Propagator::createRoot(absl::string_view trace_id,
                                                    absl::string_view span_id, bool sampled) {
  auto validation_result = validateIdsForRoot(trace_id, span_id);
  if (!validation_result.ok()) {
    return validation_result;
  }

  TraceParent root_traceparent = createRootTraceparent(trace_id, span_id, sampled);
  return TraceContext(std::move(root_traceparent));
}

bool Propagator::isValidHexString(absl::string_view input, size_t expected_length) {
  if (input.size() != expected_length) {
    return false;
  }

  return std::all_of(input.begin(), input.end(), [](char c) { return std::isxdigit(c); });
}

// Baggage-related methods

bool Propagator::isBaggagePresent(const Tracing::TraceContext& trace_context) {
  auto baggage_header = W3CConstants::get().BAGGAGE.get(trace_context);
  return baggage_header.has_value() && !baggage_header.value().empty();
}

absl::StatusOr<Baggage> Propagator::extractBaggage(const Tracing::TraceContext& trace_context) {
  auto baggage_header = W3CConstants::get().BAGGAGE.get(trace_context);
  if (!baggage_header.has_value()) {
    return Baggage(); // Return empty baggage if header not present
  }

  return Baggage::parse(baggage_header.value());
}

void Propagator::injectBaggage(const Baggage& baggage, Tracing::TraceContext& trace_context) {
  if (!baggage.empty()) {
    W3CConstants::get().BAGGAGE.set(trace_context, baggage.toString());
  }
}

// TracingHelper implementation

absl::optional<TracingHelper::ExtractedContext>
TracingHelper::extractForTracer(const Tracing::TraceContext& trace_context) {
  auto w3c_result = Propagator::extract(trace_context);
  if (!w3c_result.ok()) {
    return absl::nullopt;
  }

  const auto& w3c_context = w3c_result.value();
  const auto& traceparent = w3c_context.traceParent();

  ExtractedContext result;
  result.version = traceparent.version();
  result.trace_id = traceparent.traceId();
  result.span_id = traceparent.parentId();
  result.trace_flags = traceparent.traceFlags();
  result.sampled = traceparent.isSampled();
  result.tracestate = w3c_context.traceState().toString();

  return result;
}

bool TracingHelper::traceparentPresent(const Tracing::TraceContext& trace_context) {
  return Propagator::isPresent(trace_context);
}

// BaggageHelper implementation

std::string BaggageHelper::getBaggageValue(const Tracing::TraceContext& trace_context,
                                           absl::string_view key) {
  return extractBaggageValue(trace_context, key);
}

bool BaggageHelper::setBaggageValue(Tracing::TraceContext& trace_context, absl::string_view key,
                                    absl::string_view value) {
  return updateBaggageWithKeyValue(trace_context, key, value);
}

std::map<std::string, std::string>
BaggageHelper::getAllBaggage(const Tracing::TraceContext& trace_context) {
  return extractAllBaggageAsMap(trace_context);
}

bool BaggageHelper::hasBaggage(const Tracing::TraceContext& trace_context) {
  return Propagator::isBaggagePresent(trace_context);
}

} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
