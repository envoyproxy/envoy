#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace Wuffs {

// Parsed representation of the JSON field path used for customized target extraction.
struct ExtractFieldSpec {
  // Each segment is either a dict key (present) or an [] wildcard (absent).
  std::vector<std::optional<std::string>> segments;

  static bool is_array_element(const std::optional<std::string>& seg) { return !seg.has_value(); }
};

// Parse and convert the path string into a valid and structured ExtractFieldSpec.
// Returns InvalidArgumentError on empty/malformed input).
//
// How to write a path:
//   Navigate through your JSON until you reach the value to extract:
//     - If the current level is a JSON object: write the field name (e.g. "params")
//     - If the current level is a JSON array: write the field name followed by '[]'
//       (e.g. "messages[]") — this applies the rest of the path to each element
//     - Separate levels with '.'
//
// Syntax constraints:
//   - Field names are non-empty and cannot contain '.', '[', ']', or '\'.
//   - '[]' must immediately follow a field name or another '[]', never a '.'.
//   - A field name after '[]' must be preceded by '.'.
//   - '\' is reserved for a future escape syntax and is rejected.
//
// Example:
//   parseExtractFieldSpec("messages[].role")
//     → segments = {"messages",  // dict key
//                   nullopt,     // [] wildcard
//                   "role"},     // dict key
//                                // depth 3
//
//   parseExtractFieldSpec("model")                   // top-level scalar
//     → segments = {"model"}                         // depth 1
//
//   parseExtractFieldSpec("params._meta.traceparent") // nested dict scalar
//     → segments = {"params", "_meta", "traceparent"} // depth 3
//
//   parseExtractFieldSpec("params.protocolVersion")  // MCP initialize
//     → segments = {"params", "protocolVersion"}     // depth 2
//
//   parseExtractFieldSpec("params.message.taskId")   // A2A task correlation
//     → segments = {"params", "message", "taskId"}   // depth 3
//
//   parseExtractFieldSpec("usage.total_tokens")      // OpenAI response usage
//     → segments = {"usage", "total_tokens"}         // depth 2
//
//   parseExtractFieldSpec("tools[].function.name")   // scalar in a dict inside
//     → segments = {"tools",     // each array element
//                   nullopt,
//                   "function",
//                   "name"}      // depth 4
//
// Note: keys containing '.', '[]', or "" cannot be addressed — '.' always
// splits segments, so "vendor.example.com/token" parses as three segments
// and matches nothing at runtime. Address this in the future if needed.
absl::StatusOr<ExtractFieldSpec> parseExtractFieldSpec(absl::string_view path, int max_depth = 0);

// Configuration that controls the JSON parser. Populated at filter init.
struct ParserConfig {
  // Maximum raw body bytes to feed to cursor (0 = no limit).
  size_t max_body_bytes{0};

  // Maximum decoded bytes for a single scalar string value. Over-budget values
  // are dropped entirely (never truncated); parsing continues past them.
  // 0 = no per-value limit.
  size_t max_per_scalar_bytes{0};

  // Maximum total captured bytes across all scalar values in one body. A value
  // that would push the running total over the budget is dropped; a later
  // smaller value that still fits is captured. 0 = no total limit.
  size_t max_total_scalar_bytes{0};

  // Maximum byte span for a single captured container-element byte range. An
  // element whose [token_start, token_end) span exceeds this budget is not
  // recorded; parsing continues. 0 = no per-element limit.
  size_t max_element_capture_bytes{0};

  // When true, capture every scalar value (strings, numbers, booleans, nulls)
  // at any depth. The cursor rejects nesting deeper than kMaxTrackedDepth - 1,
  // so that is the effective ceiling.
  //
  // Mutually exclusive with extract_fields; validate() rejects parser config that
  // sets both.
  //
  // TODO(tyxia): (1) configurable max capture depth; (2) inside-array scope
  // cutoff; (3) qualified naming for nested scalars.
  bool capture_all_scalars{false};

  // Spec-based extraction: routes scalar callbacks via matchesPatternPath.
  // Mutually exclusive with capture_all_scalars.
  std::vector<ExtractFieldSpec> extract_fields;

  // Returns InvalidArgumentError if capture_all_scalars and extract_fields are
  // both set.
  absl::Status validate() const;
};

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
