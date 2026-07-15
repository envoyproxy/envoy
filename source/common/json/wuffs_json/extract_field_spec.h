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
// the target field. Pass requiredMaxDepth() from a DecoderConfig to the
// WuffsJsonCursor constructor to tighten the per-request DoS depth bound to
// exactly what the policy requires instead of the static compile-time default.
struct ExtractFieldSpec {
  // One step in the path: either a dict key or an array wildcard.
  struct Segment {
    std::string key;              // non-empty when is_array_element is false
    bool is_array_element{false};
  };

  std::string path;                 // raw path string (kept for diagnostics)
  std::vector<Segment> segments;    // parsed representation

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
// Usage pattern at filter init:
//   DecoderConfig cfg = ...;    // populate from proto
//   int depth = cfg.requiredMaxDepth();
//   WuffsJsonCursor cursor(handler, track_paths,
//                          depth > 0 ? depth : WuffsJsonCursor::kMaxDepth);
struct DecoderConfig {
  // Maximum raw body bytes to feed into the cursor.
  size_t max_body_bytes{0};

  // Maximum decoded bytes to capture for a single inline scalar string value.
  // Maps to the cursor's per-value capture budget (WuffsJsonCursor::kMaxDepth
  // analog for values). 0 = use the cursor's default (kDefaultMaxCaptureBytes).
  size_t max_inline_bytes{0};

  // Maximum raw bytes allowed in a single container byte-range capture
  // (e.g., a messages[] element or params.arguments blob).
  // 0 = no per-element limit.
  size_t max_element_capture_bytes{0};

  // Operator-declared JSON fields to extract from the body. The handler
  // matches buildPatternPath() output against spec.canonicalPath() at each
  // callback to route extraction.
  std::vector<ExtractFieldSpec> extract_fields;

  // Returns the minimum cursor max_depth needed to reach all declared fields.
  // Returns 0 when extract_fields is empty (caller should use the cursor default).
  // The caller is responsible for clamping the result to WuffsJsonCursor::kMaxDepth.
  int requiredMaxDepth() const;
};

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
