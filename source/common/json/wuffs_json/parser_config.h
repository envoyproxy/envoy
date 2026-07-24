#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace Wuffs {

// Parsed representation of a JSON field path used for extraction.
//
//   "model"                    → depth 1, top-level scalar
//   "messages[]"               → depth 2, every element of an array
//   "messages[].role"          → depth 3, field inside each array element
//   "params._meta.traceparent" → depth 3, nested dict scalar
//
// Note: keys containing '.', '[]', or "" cannot be addressed — '.' always
// splits segments, so "vendor.example.com/token" parses as three segments
// and matches nothing at runtime.
struct ExtractFieldSpec {
  struct Segment {
    std::string key; // non-empty when is_array_element is false
    bool is_array_element{false};
  };

  std::string path;              // raw path string (kept for diagnostics)
  std::vector<Segment> segments; // parsed representation

  int depth() const { return static_cast<int>(segments.size()); }

  // Reconstructs the canonical path string, e.g. "messages[].role".
  std::string canonicalPath() const;
};

// Parses and converts the path string into a valid and structured ExtractFieldSpec.
// Returns InvalidArgumentError on empty/malformed input, or if '\' appears
// (reserved for a future key-escape syntax). When max_depth > 0, also rejects
// paths deeper than max_depth — pass kMaxTrackedDepth - 1 to refuse specs the
// cursor can never match. 0 = no depth check.
//
/// Example:
//   parseExtractFieldSpec("messages[].role")
//     → ExtractFieldSpec{
//         path     = "messages[].role",
//         segments = {{key="messages", is_array_element=false},   // dict key
//                     {key="",         is_array_element=true},    // [] wildcard
//                     {key="role",     is_array_element=false}},  // dict key
//       }                                            // depth() == 3
//
//   parseExtractFieldSpec("model")                   // top-level scalar
//     → segments = {{key="model"}}                   // depth() == 1
//
//   parseExtractFieldSpec("params._meta.traceparent") // nested dict scalar
//     → segments = {{key="params"}, {key="_meta"}, {key="traceparent"}}
//                                                    // depth() == 3
//
//   parseExtractFieldSpec("params.protocolVersion")  // MCP initialize
//     → segments = {{key="params"}, {key="protocolVersion"}}
//                                                    // depth() == 2
//
//   parseExtractFieldSpec("params.message.taskId")   // A2A task correlation
//     → segments = {{key="params"}, {key="message"}, {key="taskId"}}
//                                                    // depth() == 3
//
//   parseExtractFieldSpec("usage.total_tokens")      // OpenAI response usage
//     → segments = {{key="usage"}, {key="total_tokens"}}
//                                                    // depth() == 2
//
//   parseExtractFieldSpec("tools[].function.name")   // scalar in a dict inside
//     → segments = {{key="tools"},                  //   each array element
//                   {key="", is_array_element=true},
//                   {key="function"},
//                   {key="name"}}                    // depth() == 4
absl::StatusOr<ExtractFieldSpec> parseExtractFieldSpec(absl::string_view path, int max_depth = 0);

// Configuration that controls the JSON parser. Populated at filter init.
struct ParserConfig {
  // Maximum raw body bytes to feed to cursor (0 = no limit).
  size_t max_body_bytes{0};

  // Maximum decoded bytes for a single scalar string value. Over-budget values
  // are dropped entirely (never truncated); parsing continues past them.
  // 0 = no per-value limit.
  size_t max_scalar_capture_bytes{0};

  // Maximum total captured bytes across all scalar values in one body. A value
  // that would push the running total over the budget is dropped; a later
  // smaller value that still fits is captured. 0 = no total limit.
  size_t max_total_capture_bytes{0};

  // Maximum byte span for a single captured container-element byte range. An
  // element whose [token_start, token_end) span exceeds this budget is not
  // recorded; parsing continues. 0 = no per-element limit.
  size_t max_element_capture_bytes{0};

  // When true, capture every scalar value (strings, numbers, booleans, nulls)
  // at any depth. The cursor rejects nesting deeper than kMaxTrackedDepth - 1,
  // so that is the effective ceiling.
  //
  // Mutually exclusive with extract_fields; validate() rejects a config that
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
