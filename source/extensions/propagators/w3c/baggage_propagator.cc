#include "source/extensions/propagators/w3c/baggage_propagator.h"

#include "absl/strings/string_view.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_join.h"
#include "absl/strings/ascii.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {

BaggagePropagator::BaggagePropagator() : baggage_header_("baggage") {}

absl::StatusOr<SpanContext> BaggagePropagator::extract(const Tracing::TraceContext&) {
  // Baggage propagator doesn't extract span context, only baggage
  return absl::UnimplementedError("Baggage propagator does not extract span context");
}

void BaggagePropagator::inject(const SpanContext&, Tracing::TraceContext&) {
  // Baggage propagator doesn't inject span context, only baggage
}

absl::StatusOr<Baggage>
BaggagePropagator::extractBaggage(const Tracing::TraceContext& trace_context) {
  auto baggage_value = trace_context.getByKey(baggage_header_.key());
  if (!baggage_value.has_value()) {
    return absl::NotFoundError("baggage header not present");
  }

  return parseBaggage(baggage_value.value());
}

void BaggagePropagator::injectBaggage(const Baggage& baggage,
                                      Tracing::TraceContext& trace_context) {
  if (baggage.empty()) {
    return;
  }

  std::string baggage_value = formatBaggage(baggage);
  trace_context.setByKey(baggage_header_.key(), baggage_value);
}

std::vector<std::string> BaggagePropagator::fields() const { return {"baggage"}; }

std::string BaggagePropagator::name() const { return "baggage"; }

absl::StatusOr<Baggage> BaggagePropagator::parseBaggage(const std::string& baggage_value) {
  Baggage baggage;

  if (baggage_value.empty()) {
    return baggage;
  }

  // Split by comma to get individual baggage members
  std::vector<std::string> members = absl::StrSplit(baggage_value, ',');

  for (const auto& member : members) {
    // Split by semicolon to separate key=value from metadata
    std::vector<std::string> parts = absl::StrSplit(member, ';');
    if (parts.empty()) {
      continue;
    }

    // Parse key=value part
    std::string key_value = absl::StripAsciiWhitespace(parts[0]);
    if (key_value.empty()) {
      continue;
    }

    size_t equals_pos = key_value.find('=');
    if (equals_pos == std::string::npos) {
      // Invalid format, skip this member
      continue;
    }

    std::string key = absl::StripAsciiWhitespace(key_value.substr(0, equals_pos));
    std::string value = absl::StripAsciiWhitespace(key_value.substr(equals_pos + 1));

    if (key.empty()) {
      continue; // Invalid key
    }

    // For this simplified implementation, we don't do URL encoding/decoding
    // In a full implementation, this would properly URL decode according to RFC 3986

    baggage.set(key, value);
  }

  return baggage;
}

std::string BaggagePropagator::formatBaggage(const Baggage& baggage) {
  std::vector<std::string> members;

  for (const auto& entry : baggage.entries()) {
    const std::string& key = entry.first;
    const std::string& value = entry.second;

    // For this simplified implementation, we don't do URL encoding
    // In a full implementation, this would properly URL encode according to RFC 3986

    std::string member = key + "=" + value;
    members.push_back(member);
  }

  return absl::StrJoin(members, ",");
}

} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
