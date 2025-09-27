#include "source/extensions/propagators/opentelemetry/w3c/baggage_propagator.h"

#include "source/common/common/macros.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace OpenTelemetry {

namespace {

// W3C Baggage specification constants
constexpr size_t kMaxBaggageHeaderSize = 8192; // 8KB limit per W3C spec
constexpr size_t kMaxBaggageKeySize = 256;
constexpr size_t kMaxBaggageValueSize = 4096;

// Characters allowed in baggage keys (tchar from RFC 7230)
bool isValidKeyChar(char c) {
  return absl::ascii_isalnum(c) || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
         c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' || c == '_' ||
         c == '`' || c == '|' || c == '~';
}

// Characters allowed in baggage values (visible VCHAR except delimiters)
bool isValidValueChar(char c) {
  return (c >= 0x21 && c <= 0x7E) && c != ',' && c != ';' && c != '\\' && c != '"';
}

} // namespace

BaggagePropagator::BaggagePropagator() : baggage_header_("baggage") {}

absl::StatusOr<SpanContext> BaggagePropagator::extract(const Tracing::TraceContext& trace_context) {
  // Parse baggage header to validate format and potentially store for future use
  auto baggage_header_value = baggage_header_.get(trace_context);
  if (baggage_header_value.has_value()) {
    // Parse and validate baggage entries, but don't use them yet
    // This ensures the baggage header is well-formed
    auto baggage_entries = parseBaggage(baggage_header_value.value());
    // In the future, when SpanContext supports baggage, we could store these entries
    UNREFERENCED_PARAMETER(baggage_entries);
  }

  // Baggage propagator doesn't extract trace context per OpenTelemetry specification
  // It only handles baggage propagation, which is orthogonal to trace context
  // This should return an error to indicate no trace context was extracted
  // but without affecting any previously valid trace context per the spec requirement:
  // "If a value can not be parsed from the carrier, the implementation MUST NOT store a new value
  // in the Context"
  return absl::InvalidArgumentError("Baggage propagator cannot extract span context");
}

void BaggagePropagator::inject(const SpanContext& span_context,
                               Tracing::TraceContext& trace_context) {
  UNREFERENCED_PARAMETER(span_context);

  // Per OpenTelemetry specification, baggage propagator should inject baggage headers
  // if baggage is present. Since Envoy's current SpanContext doesn't store baggage,
  // we cannot inject any baggage at this time.
  //
  // In a full implementation following W3C Baggage spec, this would:
  // 1. Extract baggage from the current context (when SpanContext supports it)
  // 2. Serialize baggage entries to "baggage" header format using formatBaggage()
  // 3. Inject the "baggage" header into the trace context
  //
  // Example future implementation:
  // if (!span_context.baggage().empty()) {
  //   std::string baggage_value = formatBaggage(span_context.baggage());
  //   baggage_header_.set(trace_context, baggage_value);
  // }
  //
  // For now, this ensures the propagator can be safely included in multi-propagator
  // configurations without breaking injection behavior.
  UNREFERENCED_PARAMETER(trace_context);
}

std::vector<std::string> BaggagePropagator::fields() const { return {"baggage"}; }

std::string BaggagePropagator::name() const { return "baggage"; }

absl::flat_hash_map<std::string, std::string>
BaggagePropagator::parseBaggage(absl::string_view baggage_header) {
  absl::flat_hash_map<std::string, std::string> baggage_entries;

  // Check header size limit
  if (baggage_header.size() > kMaxBaggageHeaderSize) {
    return baggage_entries; // Return empty map for oversized headers
  }

  // Split by comma to get individual baggage entries
  std::vector<absl::string_view> entries = absl::StrSplit(baggage_header, ',');

  for (absl::string_view entry : entries) {
    // Trim whitespace
    entry = absl::StripAsciiWhitespace(entry);
    if (entry.empty()) {
      continue;
    }

    // Split by first '=' to get key and value+properties
    size_t eq_pos = entry.find('=');
    if (eq_pos == absl::string_view::npos) {
      continue; // Invalid entry, skip
    }

    absl::string_view key = absl::StripAsciiWhitespace(entry.substr(0, eq_pos));
    absl::string_view value_and_props = entry.substr(eq_pos + 1);

    // Split value from properties (separated by ';')
    size_t semicolon_pos = value_and_props.find(';');
    absl::string_view value = absl::StripAsciiWhitespace(
        semicolon_pos == absl::string_view::npos ? value_and_props
                                                 : value_and_props.substr(0, semicolon_pos));

    // Validate and decode key and value
    std::string decoded_key = urlDecode(key);
    std::string decoded_value = urlDecode(value);

    if (!decoded_key.empty() && !decoded_value.empty() && isValidBaggageKey(decoded_key) &&
        isValidBaggageValue(decoded_value)) {
      baggage_entries[decoded_key] = decoded_value;
    }
  }

  return baggage_entries;
}

std::string BaggagePropagator::formatBaggage(
    const absl::flat_hash_map<std::string, std::string>& baggage_entries) {
  if (baggage_entries.empty()) {
    return "";
  }

  std::vector<std::string> formatted_entries;
  formatted_entries.reserve(baggage_entries.size());

  for (const auto& [key, value] : baggage_entries) {
    if (isValidBaggageKey(key) && isValidBaggageValue(value)) {
      std::string encoded_key = urlEncode(key);
      std::string encoded_value = urlEncode(value);
      formatted_entries.push_back(absl::StrCat(encoded_key, "=", encoded_value));
    }
  }

  std::string result = absl::StrJoin(formatted_entries, ",");

  // Check size limit
  if (result.size() > kMaxBaggageHeaderSize) {
    return ""; // Return empty string if too large
  }

  return result;
}

std::string BaggagePropagator::urlDecode(absl::string_view input) {
  if (input.empty()) {
    return "";
  }

  // For W3C baggage, URL decoding is primarily about percent-encoding
  // For now, we'll implement a simple approach that validates characters
  // and returns the input if valid, empty string if invalid

  std::string result;
  result.reserve(input.size());

  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      // Handle percent-encoded characters
      std::string hex_str(input.substr(i + 1, 2));
      if (hex_str.size() == 2 && absl::ascii_isxdigit(hex_str[0]) &&
          absl::ascii_isxdigit(hex_str[1])) {
        int hex_value = std::stoi(hex_str, nullptr, 16);
        result += static_cast<char>(hex_value);
        i += 2; // Skip the hex digits
      } else {
        return ""; // Invalid percent-encoding
      }
    } else {
      result += input[i];
    }
  }

  return result;
}

std::string BaggagePropagator::urlEncode(absl::string_view input) {
  if (input.empty()) {
    return "";
  }

  std::string result;
  result.reserve(input.size() * 3); // Worst case: every char encoded

  for (char c : input) {
    // Encode characters that need encoding per W3C baggage spec
    if (absl::ascii_isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      result += c;
    } else {
      absl::StrAppendFormat(&result, "%%%02X", static_cast<unsigned char>(c));
    }
  }

  return result;
}

bool BaggagePropagator::isValidBaggageKey(absl::string_view key) {
  if (key.empty() || key.size() > kMaxBaggageKeySize) {
    return false;
  }

  return std::all_of(key.begin(), key.end(), isValidKeyChar);
}

bool BaggagePropagator::isValidBaggageValue(absl::string_view value) {
  if (value.size() > kMaxBaggageValueSize) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), isValidValueChar);
}

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
