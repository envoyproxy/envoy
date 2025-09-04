#include "source/extensions/propagators/w3c/trace_context.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {

// TraceParent implementation

TraceParent::TraceParent(absl::string_view version, absl::string_view trace_id,
                         absl::string_view parent_id, absl::string_view trace_flags)
    : version_(version), trace_id_(trace_id), parent_id_(parent_id), trace_flags_(trace_flags) {}

absl::StatusOr<TraceParent> TraceParent::parse(absl::string_view traceparent_value) {
  // Validate overall length
  if (traceparent_value.size() != Constants::kTraceparentHeaderSize) {
    return absl::InvalidArgumentError("Invalid traceparent header length");
  }

  // Split by hyphens - should result in exactly 4 parts
  std::vector<absl::string_view> parts = absl::StrSplit(traceparent_value, '-', absl::SkipEmpty());
  if (parts.size() != 4) {
    return absl::InvalidArgumentError(
        "Invalid traceparent format: must have 4 hyphen-separated fields");
  }

  absl::string_view version = parts[0];
  absl::string_view trace_id = parts[1];
  absl::string_view parent_id = parts[2];
  absl::string_view trace_flags = parts[3];

  // Validate field lengths
  if (version.size() != Constants::kVersionSize || trace_id.size() != Constants::kTraceIdSize ||
      parent_id.size() != Constants::kParentIdSize ||
      trace_flags.size() != Constants::kTraceFlagsSize) {
    return absl::InvalidArgumentError("Invalid traceparent field sizes");
  }

  // Validate hex encoding
  if (!isValidHex(version) || !isValidHex(trace_id) || !isValidHex(parent_id) ||
      !isValidHex(trace_flags)) {
    return absl::InvalidArgumentError("Invalid traceparent hex encoding");
  }

  // Validate that trace-id and parent-id are not all zeros
  if (isAllZeros(trace_id)) {
    return absl::InvalidArgumentError("Invalid traceparent: trace-id cannot be all zeros");
  }
  if (isAllZeros(parent_id)) {
    return absl::InvalidArgumentError("Invalid traceparent: parent-id cannot be all zeros");
  }

  return TraceParent(version, trace_id, parent_id, trace_flags);
}

std::string TraceParent::toString() const {
  return absl::StrJoin({version_, trace_id_, parent_id_, trace_flags_}, "-");
}

bool TraceParent::isSampled() const {
  // Parse trace_flags as hex and check sampled bit
  std::string decoded = absl::HexStringToBytes(trace_flags_);
  if (decoded.empty()) {
    return false;
  }
  return (static_cast<uint8_t>(decoded[0]) & Constants::kSampledFlag) != 0;
}

void TraceParent::setSampled(bool sampled) {
  // Parse current flags
  std::string decoded = absl::HexStringToBytes(trace_flags_);
  if (decoded.empty()) {
    decoded = "\x00";
  }

  uint8_t flags = static_cast<uint8_t>(decoded[0]);
  if (sampled) {
    flags |= Constants::kSampledFlag;
  } else {
    flags &= ~Constants::kSampledFlag;
  }

  // Convert back to hex string
  trace_flags_ = absl::BytesToHexString(std::string(1, static_cast<char>(flags)));

  // Ensure uppercase hex (W3C spec uses lowercase, but be consistent)
  std::transform(trace_flags_.begin(), trace_flags_.end(), trace_flags_.begin(), ::tolower);
}

bool TraceParent::isValidHex(absl::string_view input) {
  return std::all_of(input.begin(), input.end(), [](char c) { return std::isxdigit(c); });
}

bool TraceParent::isAllZeros(absl::string_view input) {
  return std::all_of(input.begin(), input.end(), [](char c) { return c == '0'; });
}

// TraceState implementation

TraceState::TraceState(absl::string_view tracestate_value) {
  // If empty, nothing to do
  if (tracestate_value.empty()) {
    return;
  }

  // Parse comma-separated key=value pairs
  std::vector<absl::string_view> entries = absl::StrSplit(tracestate_value, ',');

  for (absl::string_view entry : entries) {
    entry = absl::StripAsciiWhitespace(entry);
    if (entry.empty()) {
      continue;
    }

    // Split on first '=' only
    std::vector<absl::string_view> kv = absl::StrSplit(entry, absl::MaxSplits('=', 1));
    if (kv.size() != 2) {
      continue; // Skip invalid entries
    }

    absl::string_view key = absl::StripAsciiWhitespace(kv[0]);
    absl::string_view value = absl::StripAsciiWhitespace(kv[1]);

    if (isValidKey(key) && isValidValue(value)) {
      entries_.emplace_back(key, value);
    }
  }
}

absl::StatusOr<TraceState> TraceState::parse(absl::string_view tracestate_value) {
  TraceState result(tracestate_value);
  return result;
}

std::string TraceState::toString() const {
  if (entries_.empty()) {
    return "";
  }

  std::vector<std::string> formatted_entries;
  formatted_entries.reserve(entries_.size());

  for (const auto& entry : entries_) {
    formatted_entries.push_back(absl::StrJoin({entry.first, entry.second}, "="));
  }

  return absl::StrJoin(formatted_entries, ",");
}

absl::optional<absl::string_view> TraceState::get(absl::string_view key) const {
  for (const auto& entry : entries_) {
    if (entry.first == key) {
      return entry.second;
    }
  }
  return absl::nullopt;
}

void TraceState::set(absl::string_view key, absl::string_view value) {
  if (!isValidKey(key) || !isValidValue(value)) {
    return; // Ignore invalid entries
  }

  // Remove existing entry with same key
  remove(key);

  // Add new entry at the beginning (as per W3C spec, most recent should be first)
  entries_.insert(entries_.begin(), std::make_pair(std::string(key), std::string(value)));
}

void TraceState::remove(absl::string_view key) {
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&key](const std::pair<std::string, std::string>& entry) {
                                  return entry.first == key;
                                }),
                 entries_.end());
}

bool TraceState::isValidKey(absl::string_view key) {
  if (key.empty() || key.size() > 256) {
    return false;
  }

  // W3C spec: key must start with lowercase letter or digit,
  // and contain only lowercase letters, digits, underscores, hyphens, asterisks, forward slashes
  for (size_t i = 0; i < key.size(); ++i) {
    char c = key[i];
    bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                 c == '*' || c == '/';
    if (!valid) {
      return false;
    }
  }

  return true;
}

bool TraceState::isValidValue(absl::string_view value) {
  if (value.empty() || value.size() > 256) {
    return false;
  }

  // W3C spec: value can contain any printable ASCII except comma, semicolon, and space
  for (char c : value) {
    if (c < 0x20 || c > 0x7E || c == ',' || c == ';' || c == ' ') {
      return false;
    }
  }

  return true;
}

// TraceContext implementation

TraceContext::TraceContext(TraceParent traceparent) : traceparent_(std::move(traceparent)) {}

TraceContext::TraceContext(TraceParent traceparent, TraceState tracestate)
    : traceparent_(std::move(traceparent)), tracestate_(std::move(tracestate)) {}

TraceContext::TraceContext(TraceParent traceparent, TraceState tracestate, Baggage baggage)
    : traceparent_(std::move(traceparent)), tracestate_(std::move(tracestate)),
      baggage_(std::move(baggage)) {}

// BaggageMember implementation

BaggageMember::BaggageMember(absl::string_view key, absl::string_view value)
    : key_(urlDecode(key)), value_(urlDecode(value)) {}

BaggageMember::BaggageMember(absl::string_view key, absl::string_view value,
                             const std::vector<std::pair<std::string, std::string>>& properties)
    : key_(urlDecode(key)), value_(urlDecode(value)), properties_(properties) {}

absl::StatusOr<BaggageMember> BaggageMember::parse(absl::string_view member_string) {
  // Trim whitespace
  member_string = absl::StripAsciiWhitespace(member_string);

  if (member_string.empty()) {
    return absl::InvalidArgumentError("Empty baggage member");
  }

  // Split by semicolon to separate key=value from properties
  std::vector<absl::string_view> parts = absl::StrSplit(member_string, ';');

  if (parts.empty()) {
    return absl::InvalidArgumentError("Invalid baggage member format");
  }

  // Parse key=value
  absl::string_view key_value = absl::StripAsciiWhitespace(parts[0]);
  std::vector<absl::string_view> kv = absl::StrSplit(key_value, absl::MaxSplits('=', 1));

  if (kv.size() != 2) {
    return absl::InvalidArgumentError("Baggage member must have key=value format");
  }

  absl::string_view key = absl::StripAsciiWhitespace(kv[0]);
  absl::string_view value = absl::StripAsciiWhitespace(kv[1]);

  if (key.empty()) {
    return absl::InvalidArgumentError("Baggage key cannot be empty");
  }

  if (!isValidKey(key)) {
    return absl::InvalidArgumentError("Invalid baggage key format");
  }

  if (!isValidValue(value)) {
    return absl::InvalidArgumentError("Invalid baggage value format");
  }

  // Parse properties
  std::vector<std::pair<std::string, std::string>> properties;
  for (size_t i = 1; i < parts.size(); ++i) {
    absl::string_view property = absl::StripAsciiWhitespace(parts[i]);
    if (property.empty())
      continue;

    std::vector<absl::string_view> prop_kv = absl::StrSplit(property, absl::MaxSplits('=', 1));
    std::string prop_key = std::string(absl::StripAsciiWhitespace(prop_kv[0]));
    std::string prop_value =
        prop_kv.size() > 1 ? std::string(absl::StripAsciiWhitespace(prop_kv[1])) : "";

    properties.emplace_back(std::move(prop_key), std::move(prop_value));
  }

  return BaggageMember(key, value, properties);
}

std::string BaggageMember::toString() const {
  std::string result = urlEncode(key_) + "=" + urlEncode(value_);

  for (const auto& prop : properties_) {
    result += ";" + prop.first;
    if (!prop.second.empty()) {
      result += "=" + prop.second;
    }
  }

  return result;
}

absl::optional<absl::string_view> BaggageMember::getProperty(absl::string_view property_key) const {
  for (const auto& prop : properties_) {
    if (prop.first == property_key) {
      return prop.second;
    }
  }
  return absl::nullopt;
}

void BaggageMember::setProperty(absl::string_view property_key, absl::string_view property_value) {
  // Replace existing property or add new one
  for (auto& prop : properties_) {
    if (prop.first == property_key) {
      prop.second = std::string(property_value);
      return;
    }
  }
  properties_.emplace_back(std::string(property_key), std::string(property_value));
}

bool BaggageMember::isValidKey(absl::string_view key) {
  if (key.empty() || key.length() > Constants::kMaxKeyLength) {
    return false;
  }

  // Keys must be URL-safe: alphanumeric, dash, underscore, period
  for (char c : key) {
    if (!std::isalnum(c) && c != '-' && c != '_' && c != '.') {
      return false;
    }
  }
  return true;
}

bool BaggageMember::isValidValue(absl::string_view value) {
  if (value.length() > Constants::kMaxValueLength) {
    return false;
  }

  // Values can contain any printable ASCII character except control characters
  for (char c : value) {
    if (c < 0x20 || c > 0x7E) {
      return false;
    }
  }
  return true;
}

std::string BaggageMember::urlDecode(absl::string_view input) {
  std::string result;
  result.reserve(input.size());

  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      // Decode %XX
      std::string hex = std::string(input.substr(i + 1, 2));
      char* end;
      long val = std::strtol(hex.c_str(), &end, 16);
      if (end == hex.c_str() + 2) {
        result += static_cast<char>(val);
        i += 2;
      } else {
        result += input[i];
      }
    } else {
      result += input[i];
    }
  }

  return result;
}

std::string BaggageMember::urlEncode(absl::string_view input) {
  std::ostringstream encoded;
  encoded << std::hex << std::uppercase;

  for (unsigned char c : input) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded << c;
    } else {
      encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
  }

  return encoded.str();
}

// Baggage implementation

Baggage::Baggage(absl::string_view baggage_value) {
  auto parsed = parse(baggage_value);
  if (parsed.ok()) {
    *this = std::move(parsed.value());
  }
}

absl::StatusOr<Baggage> Baggage::parse(absl::string_view baggage_value) {
  Baggage baggage;

  if (baggage_value.empty()) {
    return baggage;
  }

  // Split by comma to get individual members
  std::vector<absl::string_view> members = absl::StrSplit(baggage_value, ',');

  for (absl::string_view member_str : members) {
    member_str = absl::StripAsciiWhitespace(member_str);
    if (member_str.empty()) {
      continue;
    }

    auto member = BaggageMember::parse(member_str);
    if (!member.ok()) {
      return member.status();
    }

    if (!baggage.wouldExceedLimits(member.value())) {
      baggage.members_.push_back(std::move(member.value()));
    } else {
      return absl::ResourceExhaustedError("Baggage size limits exceeded");
    }
  }

  return baggage;
}

std::string Baggage::toString() const {
  std::vector<std::string> member_strings;
  member_strings.reserve(members_.size());

  for (const auto& member : members_) {
    member_strings.push_back(member.toString());
  }

  return absl::StrJoin(member_strings, ", ");
}

absl::optional<absl::string_view> Baggage::get(absl::string_view key) const {
  for (const auto& member : members_) {
    if (member.key() == key) {
      return member.value();
    }
  }
  return absl::nullopt;
}

bool Baggage::set(absl::string_view key, absl::string_view value) {
  BaggageMember new_member(key, value);
  return set(new_member);
}

bool Baggage::set(const BaggageMember& member) {
  // Check if adding this member would exceed limits
  auto existing_index = findMemberIndex(member.key());

  // Calculate size impact
  size_t size_change = member.toString().size();
  if (existing_index.has_value()) {
    size_change -= members_[existing_index.value()].toString().size();
  }

  if (size() + size_change > Constants::kMaxBaggageSize) {
    return false;
  }

  if (!existing_index.has_value() && members_.size() >= Constants::kMaxBaggageMembers) {
    return false;
  }

  // Update or add member
  if (existing_index.has_value()) {
    members_[existing_index.value()] = member;
  } else {
    members_.push_back(member);
  }

  return true;
}

void Baggage::remove(absl::string_view key) {
  auto it = std::remove_if(members_.begin(), members_.end(),
                           [key](const BaggageMember& member) { return member.key() == key; });
  members_.erase(it, members_.end());
}

size_t Baggage::size() const { return toString().size(); }

bool Baggage::wouldExceedLimits(const BaggageMember& member) const {
  auto existing_index = findMemberIndex(member.key());

  size_t size_change = member.toString().size();
  if (existing_index.has_value()) {
    size_change -= members_[existing_index.value()].toString().size();
  }

  if (size() + size_change > Constants::kMaxBaggageSize) {
    return true;
  }

  if (!existing_index.has_value() && members_.size() >= Constants::kMaxBaggageMembers) {
    return true;
  }

  return false;
}

absl::optional<size_t> Baggage::findMemberIndex(absl::string_view key) const {
  for (size_t i = 0; i < members_.size(); ++i) {
    if (members_[i].key() == key) {
      return i;
    }
  }
  return absl::nullopt;
}

} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
