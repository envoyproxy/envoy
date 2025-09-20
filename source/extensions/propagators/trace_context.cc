#include "source/extensions/propagators/trace_context.h"

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "source/common/common/hex.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {

namespace {

bool isValidHex(absl::string_view hex_string) {
  if (hex_string.empty()) {
    return false;
  }

  for (char c : hex_string) {
    if (!absl::ascii_isxdigit(c)) {
      return false;
    }
  }
  return true;
}

std::vector<uint8_t> hexToBytes(absl::string_view hex_string) {
  std::vector<uint8_t> bytes;
  if (hex_string.length() % 2 != 0) {
    return bytes; // Invalid hex string
  }

  for (size_t i = 0; i < hex_string.length(); i += 2) {
    std::string byte_string(hex_string.substr(i, 2));
    uint64_t byte_value;
    if (absl::SimpleHexAtoi(byte_string, &byte_value)) {
      bytes.push_back(static_cast<uint8_t>(byte_value));
    } else {
      return {}; // Invalid hex
    }
  }
  return bytes;
}

bool isZero(const std::vector<uint8_t>& bytes) {
  for (uint8_t byte : bytes) {
    if (byte != 0) {
      return false;
    }
  }
  return true;
}

} // namespace

// TraceId implementation
TraceId::TraceId(absl::string_view hex_string) {
  if (isValidHex(hex_string)) {
    hex_string_ = absl::AsciiStrToLower(hex_string);
    bytes_ = hexToBytes(hex_string_);
  }
}

bool TraceId::isValid() const { return !hex_string_.empty() && !bytes_.empty() && !isZero(bytes_); }

// SpanId implementation
SpanId::SpanId(absl::string_view hex_string) {
  if (isValidHex(hex_string)) {
    hex_string_ = absl::AsciiStrToLower(hex_string);
    bytes_ = hexToBytes(hex_string_);
  }
}

bool SpanId::isValid() const { return !hex_string_.empty() && !bytes_.empty() && !isZero(bytes_); }

// TraceFlags implementation
TraceFlags::TraceFlags(uint8_t flags) : flags_(flags) {}

void TraceFlags::setSampled(bool sampled) {
  if (sampled) {
    flags_ |= SAMPLED_FLAG;
  } else {
    flags_ &= ~SAMPLED_FLAG;
  }
}

// SpanContext implementation
SpanContext::SpanContext(TraceId trace_id, SpanId span_id, TraceFlags trace_flags,
                         absl::optional<SpanId> parent_span_id, std::string tracestate)
    : trace_id_(std::move(trace_id)), span_id_(std::move(span_id)), trace_flags_(trace_flags),
      parent_span_id_(std::move(parent_span_id)), tracestate_(std::move(tracestate)) {}

bool SpanContext::isValid() const { return trace_id_.isValid() && span_id_.isValid(); }

// Baggage implementation
Baggage::Baggage(const BaggageMap& entries) : entries_(entries) {}

void Baggage::set(const std::string& key, const std::string& value) { entries_[key] = value; }

absl::optional<std::string> Baggage::get(const std::string& key) const {
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    return it->second;
  }
  return absl::nullopt;
}

void Baggage::remove(const std::string& key) { entries_.erase(key); }

} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
