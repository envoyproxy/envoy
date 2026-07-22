#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace Wuffs {

// Describes one field to extract from the JSON body.
//
// Path syntax exactly mirrors buildPatternPath() output — dict keys separated by
// '.', with '[]' for array wildcards (no '.' precedes '[]'):
//   "model"                       depth=1, scalar under root object
//   "messages[]"                  depth=2, every element of the messages array
//   "messages[].role"             depth=3, the role field inside each element
//   "params._meta.traceparent"    depth=3, nested dict scalar
//   "messages[].content[]"        depth=4, inner array element
//
// depth() returns the number of segments, which equals the JSON nesting depth of
// the target field. DecoderConfig::requiredMaxDepth() aggregates these so filter
// init can tighten the cursor's DoS depth bound to exactly what the policy
// requires. TODO(tyxia): that needs a max_depth cursor constructor argument,
// which does not exist yet — the cursor is fixed at its compile-time
// kMaxTrackedDepth bound.
struct ExtractFieldSpec {
  // One step in the path: either a dict key or an array wildcard.
  struct Segment {
    std::string key; // non-empty when is_array_element is false
    bool is_array_element{false};
  };

  std::string path;              // raw path string (kept for diagnostics)
  std::vector<Segment> segments; // parsed representation

  // Number of path segments = nesting depth of the target field.
  int depth() const { return static_cast<int>(segments.size()); }

  // Reconstructs the canonical pattern-path string from segments, matching
  // buildPatternPath() output exactly. Useful for comparison at runtime.
  std::string canonicalPath() const;
};

// Parses a dot-notation path string into an ExtractFieldSpec.
// Returns InvalidArgumentError when the path is empty or malformed
// (leading/trailing/double '.', unmatched '['/']', non-empty subscript
// content such as '[0]', or '.' immediately before '[').
// On success, result.path == path and result.segments is populated.
absl::StatusOr<ExtractFieldSpec> parseExtractFieldSpec(absl::string_view path);

// Configuration that controls JSON decoding.
//
// Intended usage at filter init, once the cursor grows a max_depth constructor
// argument (TODO(tyxia) in wuffs_json_cursor.h — not implemented yet):
//   DecoderConfig cfg = ...;    // populate from proto
//   int depth = cfg.requiredMaxDepth();
//   WuffsJsonCursor cursor(handler, track_paths,
//                          depth > 0 ? depth : /*cursor compile-time default*/);
//
// TODO(tyxia): when the proto plumbing lands, add a validate() enforcing the
// inter-field invariants that are currently implicit: max_body_bytes > 0 in
// production, and max_inline_bytes / max_element_capture_bytes not exceeding
// max_body_bytes when non-zero.
struct DecoderConfig {
  // Maximum raw body bytes to feed into the cursor. The outer filter should
  // check cursor.nextSourcePosition() + chunk.size() against this limit before
  // each feed() call and return ResourceExhausted instead of feeding, so an
  // oversized chunk is rejected without being parsed. nextSourcePosition()
  // lags bytes actually fed by at most WuffsJsonCursor::kMaxPendingBytes (an
  // incomplete token tail), so the check can under-reject by at most that much.
  // 0 = no limit (tests only; production must set a non-zero value).
  size_t max_body_bytes{0};

  // Maximum decoded bytes to capture for a single inline scalar string value.
  // Maps to the cursor-side per-value capture budget (the kMaxKeyBytes analog
  // for values; TODO(tyxia): not implemented in the cursor yet). 0 = use the
  // cursor's default once that budget lands.
  size_t max_inline_bytes{0};

  // Maximum raw bytes allowed in a single container byte-range capture
  // (e.g., a messages[] element or params.arguments blob).
  // 0 = no per-element limit.
  size_t max_element_capture_bytes{0};

  // Operator-declared JSON fields to extract from the body. The handler
  // matches buildPatternPath() output against spec.canonicalPath() at each
  // callback to route extraction.
  //
  // TODO(tyxia): plain string equality is not collision-free. Document keys
  // may contain '.', '[', ']' or be empty ("" is a legal JSON key), letting a
  // hostile body synthesize the same (string, depth) pair as a legitimately
  // nested field — e.g. {"":{"a.b":"decoy"}} collides with spec "a.b" meaning
  // {"a":{"b":...}} (see StringEqualityCollidesOnHostileKeys in
  // extract_field_spec_test.cc). The production handler must match
  // spec.segments structurally (label by label) or enforce key hygiene,
  // not trust the serialized string.
  std::vector<ExtractFieldSpec> extract_fields;

  // Returns the minimum cursor max_depth needed to reach all declared fields.
  // Returns 0 when extract_fields is empty (caller should use the cursor default).
  // The caller is responsible for clamping the result to the cursor's
  // compile-time depth bound (WuffsJsonCursor::kMaxTrackedDepth - 1).
  int requiredMaxDepth() const;
};

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
