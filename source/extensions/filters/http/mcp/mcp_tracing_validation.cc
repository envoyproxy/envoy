#include "source/extensions/filters/http/mcp/mcp_tracing_validation.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

namespace {

// W3C Trace Context constants
constexpr size_t kTraceParentExpectedSize = 55;
constexpr size_t kVersionHexSize = 2;
constexpr size_t kTraceIdHexSize = 32;
constexpr size_t kParentIdHexSize = 16;
constexpr size_t kTraceFlagsHexSize = 2;

// W3C Baggage constants
constexpr size_t kMaxBaggageSize = 8192;
constexpr size_t kMaxBaggageMembers = 64;

bool isValidHex(absl::string_view input) {
  return std::all_of(input.begin(), input.end(),
                     [](unsigned char c) { return absl::ascii_isxdigit(c); });
}

bool isAllZeros(absl::string_view input) {
  return std::all_of(input.begin(), input.end(), [](char c) { return c == '0'; });
}

// Tracestate validation helpers
bool isValidTraceStateKeyChar(char c) {
  return absl::ascii_islower(c) || absl::ascii_isdigit(c) || c == '_' || c == '-' || c == '*' ||
         c == '/';
}

bool isValidTraceStateKey(absl::string_view key) {
  if (key.empty() || key.size() > 256) {
    return false;
  }
  // Simple keys or multi-tenant keys (tenant-id@system-id)
  auto at_pos = key.find('@');
  if (at_pos == absl::string_view::npos) {
    // simple key
    if (!absl::ascii_islower(key[0])) {
      // first char must be lowercase letter
      return false;
    }
    return std::all_of(key.begin(), key.end(), isValidTraceStateKeyChar);
  } else {
    // multi-tenant key
    absl::string_view left = key.substr(0, at_pos);
    absl::string_view right = key.substr(at_pos + 1);
    if (left.empty() || left.size() > 241 || right.empty() || right.size() > 14) {
      return false;
    }
    if (!absl::ascii_islower(left[0]) && !absl::ascii_isdigit(left[0])) {
      // first char of tenant-id must be lowercase letter or digit
      return false;
    }
    if (!absl::ascii_islower(right[0])) {
      // first char of system-id must be lowercase letter
      return false;
    }
    return std::all_of(left.begin(), left.end(), isValidTraceStateKeyChar) &&
           std::all_of(right.begin(), right.end(), isValidTraceStateKeyChar);
  }
}

bool isValidTraceStateValue(absl::string_view value) {
  if (value.size() > 256) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return c >= 0x21 && c <= 0x7e && c != ',' && c != '=';
  });
}

// Baggage validation helpers
bool isTokenChar(char c) {
  if (c <= 0x20 || c > 0x7e) {
    return false;
  }
  static constexpr absl::string_view kDelimiters = "\"(),/:;<=>?@[\\]{}";
  return !absl::StrContains(kDelimiters, c);
}

bool isBaggageOctet(char c) {
  return (c >= 0x21 && c <= 0x2b) || (c >= 0x2d && c <= 0x3a) || (c >= 0x3c && c <= 0x5b) ||
         (c >= 0x5d && c <= 0x7e);
}

bool isValidBaggageKey(absl::string_view key) {
  absl::string_view trimmed = absl::StripAsciiWhitespace(key);
  if (trimmed.empty()) {
    return false;
  }
  return std::all_of(trimmed.begin(), trimmed.end(), isTokenChar);
}

bool isValidBaggageValue(absl::string_view value) {
  absl::string_view trimmed = absl::StripAsciiWhitespace(value);
  return std::all_of(trimmed.begin(), trimmed.end(), isBaggageOctet);
}

} // namespace

namespace McpTracingValidation {

bool isValidTraceParent(absl::string_view trace_parent) {
  if (trace_parent.size() != kTraceParentExpectedSize) {
    return false;
  }

  std::vector<absl::string_view> components = absl::StrSplit(trace_parent, '-');
  if (components.size() != 4) {
    return false;
  }

  absl::string_view version = components[0];
  absl::string_view trace_id = components[1];
  absl::string_view parent_id = components[2];
  absl::string_view flags = components[3];

  if (version.size() != kVersionHexSize || trace_id.size() != kTraceIdHexSize ||
      parent_id.size() != kParentIdHexSize || flags.size() != kTraceFlagsHexSize) {
    return false;
  }

  if (!isValidHex(version) || !isValidHex(trace_id) || !isValidHex(parent_id) ||
      !isValidHex(flags)) {
    return false;
  }

  if (version == "ff") {
    return false;
  }

  if (isAllZeros(trace_id) || isAllZeros(parent_id)) {
    return false;
  }

  return true;
}

bool isValidTraceState(absl::string_view trace_state) {
  if (trace_state.empty()) {
    return true;
  }
  if (trace_state.size() > 1024) {
    return false;
  }

  std::vector<absl::string_view> members = absl::StrSplit(trace_state, ',');
  if (members.size() > 32) {
    return false;
  }

  absl::flat_hash_set<absl::string_view> keys;
  for (absl::string_view member : members) {
    absl::string_view trimmed_member = absl::StripAsciiWhitespace(member);
    if (trimmed_member.empty()) {
      continue;
    }
    std::vector<absl::string_view> kv = absl::StrSplit(trimmed_member, absl::MaxSplits('=', 1));
    if (kv.size() != 2) {
      return false;
    }
    absl::string_view key = kv[0];
    if (!isValidTraceStateKey(key) || !isValidTraceStateValue(kv[1])) {
      return false;
    }
    if (!keys.insert(key).second) {
      return false; // Duplicate key
    }
  }

  return true;
}

bool isValidBaggage(absl::string_view baggage) {
  if (baggage.empty()) {
    return true;
  }
  if (baggage.size() > kMaxBaggageSize) {
    return false;
  }

  std::vector<absl::string_view> members = absl::StrSplit(baggage, ',');
  if (members.size() > kMaxBaggageMembers) {
    return false;
  }

  for (absl::string_view member : members) {
    absl::string_view trimmed_member = absl::StripAsciiWhitespace(member);
    if (trimmed_member.empty()) {
      return false; // Baggage doesn't allow empty members
    }
    std::vector<absl::string_view> parts = absl::StrSplit(trimmed_member, absl::MaxSplits(';', 1));
    std::vector<absl::string_view> kv = absl::StrSplit(parts[0], absl::MaxSplits('=', 1));
    if (kv.size() != 2) {
      return false;
    }
    if (!isValidBaggageKey(kv[0]) || !isValidBaggageValue(kv[1])) {
      return false;
    }
    // Optional properties
    if (parts.size() == 2) {
      std::vector<absl::string_view> props = absl::StrSplit(parts[1], ';');
      for (absl::string_view prop : props) {
        std::vector<absl::string_view> pkv = absl::StrSplit(prop, absl::MaxSplits('=', 1));
        if (!isValidBaggageKey(pkv[0])) {
          return false;
        }
        if (pkv.size() == 2 && !isValidBaggageValue(pkv[1])) {
          return false;
        }
      }
    }
  }

  return true;
}

} // namespace McpTracingValidation
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
