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

// Describes the fields to extract from the JSON body.
// For example:
//   "model"                       depth=1, scalar under root object
//   "messages[]"                  depth=2, every element of the messages array
//   "messages[].role"             depth=3, the role field inside each element
//   "params._meta.traceparent"    depth=3, nested dict scalar
//   "messages[].content[]"        depth=4, inner array element
//
// depth() returns the JSON nesting depth of the target field.
//
// Note: the grammar cannot address document keys that contain '.',
// contain the substring "[]", or are the empty string "". '.' always splits
// segments, so a spec written for the single MCP-style key
// "vendor.example.com/token" parses as multiple nested segments and silently
// matches nothing at runtime. Address this later if needed.
struct ExtractFieldSpec {
  // One step in the path: either a dict key or an array wildcard.
  struct Segment {
    std::string key; // non-empty when is_array_element is false
    bool is_array_element{false};
  };

  std::string path;              // raw path string (kept for diagnostics)
  std::vector<Segment> segments; // parsed representation

  // Number of path segments (i.e., nesting depth of the target field)
  int depth() const { return static_cast<int>(segments.size()); }

  // Reconstructs the canonical pattern-path string from segments (dict keys
  // joined with '.', array wildcards as '[]', e.g. "messages[].role").
  std::string canonicalPath() const;
};

// Parses and converts the path string into a valid and structured ExtractFieldSpec.
// Returns InvalidArgumentError when the path is empty or malformed
// (leading/trailing/double '.', unmatched '['/']' etc), or contains '\' —
// reserved for a future key-escape syntax (see the note on ExtractFieldSpec).
// When max_depth > 0, also rejects paths with more than max_depth segments —
// pass the cursor's depth bound (kMaxTrackedDepth - 1) so a spec that the
// cursor could never match is refused at config load instead of silently
// matching nothing at runtime. 0 = no depth check.
// On success, result.path == path and result.segments is populated.
//
// Example:
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
//
absl::StatusOr<ExtractFieldSpec> parseExtractFieldSpec(absl::string_view path, int max_depth = 0);

// Configuration that controls JSON parser.
//
// Populated at filter init, before any request flows.
struct ParserConfig {
  // Maximum raw body bytes to feed into the cursor. The outer filter should
  // check cursor.nextSourcePosition() + chunk.size() against this limit before
  // each feed() call and return ResourceExhausted instead of feeding, so an
  // oversized chunk is rejected without being parsed. nextSourcePosition()
  // lags bytes actually fed by at most WuffsJsonCursor::kMaxPendingBytes (an
  // incomplete token tail), so the check can under-reject by at most that much.
  // 0 = no limit (tests only; production must set a non-zero value).
  size_t max_body_bytes{0};

  // Maximum decoded bytes for a single inline scalar string value. A value
  // whose decoded size exceeds the budget is rejected — dropped entirely,
  // never captured truncated — while parsing continues past it (see
  // CaptureAllScalarsHandler in parser_config_test.cc).
  // Maps to the cursor-side per-value capture budget (the kMaxKeyBytes analog
  // for values; TODO(tyxia): not implemented in the cursor yet).
  // 0 = no per-value limit.
  size_t max_scalar_capture_bytes{0};

  // Maximum total captured bytes across all values of one body — the
  // body-wide retention budget on top of the per-scalar cap above, which
  // bounds each capture individually but not how many captures a body can
  // produce. A value whose size would push the running total over the budget
  // is rejected — dropped entirely, same semantics as
  // max_scalar_capture_bytes — while parsing continues, and a later smaller
  // value that still fits is captured (see CaptureAllScalarsHandler in
  // parser_config_test.cc). Counts recorded value bytes only; keys are
  // already bounded by the cursor's kMaxKeyBytes.
  size_t max_total_capture_bytes{0};

  // Maximum byte span for a single captured container-element byte range.
  // A container element whose [token_start, token_end) span exceeds this
  // budget is rejected — its range is not recorded — while parsing continues
  // past it (see ContainerRangeHandler in parser_config_test.cc).
  // 0 = no per-element limit.
  size_t max_element_capture_bytes{0};

  // When true, capture every scalar value in the body (strings, numbers,
  // booleans, nulls) at any depth from cursor. The cursor rejects nesting deeper
  // than kMaxTrackedDepth - 1, so that is the effective ceiling.
  //
  // Mutually exclusive with extract_fields — exactly one extraction mode may
  // be active; validate() rejects a config that sets both.
  //
  // TODO(tyxia): (1) max capture depth independent of (smaller than) kMaxTrackedDepth
  // (2) inside-array scope cutoff;
  // (3) qualified naming for nested scalars (the leaf key alone is ambiguous across parents).
  bool capture_all_scalars{false};

  // Extraction policy to extract JSON fields from the body.
  //
  // The handler routes extraction by structural matching: convert each spec's
  // segments to WuffsJsonCursor::PatternSegment once at init, then call
  // cursor.matchesPatternPath(segments, depth) at each callback.
  //
  // Mutually exclusive with capture_all_scalars (see above).
  std::vector<ExtractFieldSpec> extract_fields;

  // Checks the inter-field invariants: capture_all_scalars and extract_fields
  // are mutually exclusive extraction modes, so a config that sets both is
  // rejected with InvalidArgumentError.
  absl::Status validate() const;
};

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
