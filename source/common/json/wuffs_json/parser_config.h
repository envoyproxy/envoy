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
// depth() returns the the JSON nesting depth of the target field.
// ParserConfig::requiredMaxDepth() aggregates these so filter
// init can tighten the cursor's DoS depth bound to exactly what the policy
// requires.
// TODO(tyxia): that needs a max_depth cursor constructor argument,
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

  // Number of path segments (i.e., nesting depth of the target field)
  int depth() const { return static_cast<int>(segments.size()); }

  // Reconstructs the canonical pattern-path string from segments (dict keys
  // joined with '.', array wildcards as '[]', e.g. "messages[].role").
  // Diagnostics and config echo only — runtime routing must match `segments`
  // structurally via WuffsJsonCursor::matchesPatternPath() (see
  // extract_fields below).
  std::string canonicalPath() const;
};

// Parses and convert the path string from config into a valid and structured
// ExtractFieldSpec.
// Returns InvalidArgumentError when the path is empty or malformed
// (leading/trailing/double '.', unmatched '['/']', non-empty subscript
// content such as '[0]', '.' immediately before '[', or a key starting
// directly after ']' without a '.' separator).
// When max_depth > 0, also rejects paths with more than max_depth segments —
// pass the cursor's depth bound (kMaxTrackedDepth - 1) so a spec that the
// cursor could never match is refused at config load instead of silently
// matching nothing at runtime. 0 = no depth check.
// On success, result.path == path and result.segments is populated.
absl::StatusOr<ExtractFieldSpec> parseExtractFieldSpec(absl::string_view path, int max_depth = 0);

// Configuration that controls JSON decoding.
//
// Intended usage at filter init, once the cursor grows a max_depth constructor
// argument.
//   ParserConfig cfg = ...;    // populated at the time when parser is initialized.
//   int depth = cfg.requiredMaxDepth();
//   WuffsJsonCursor cursor(handler, track_paths,
//                          depth > 0 ? depth : /*cursor compile-time default*/);
//
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
  // for values; TODO(tyxia): not implemented in the cursor yet). 0 = use the
  // cursor's default once that budget lands.
  size_t max_scalar_capture_bytes{0};

  // Maximum raw bytes allowed in a single container byte-range capture
  // (e.g., a messages[] element or params.arguments blob).
  // 0 = no per-element limit.
  size_t max_element_capture_bytes{0};

  // Maximum total captured bytes across all values of one body — the
  // body-wide retention budget on top of the per-scalar / per-element caps
  // above, which bound each capture individually but not how many captures a
  // body can produce. A value whose size would push the running total over
  // the budget is rejected — dropped entirely, same semantics as
  // max_scalar_capture_bytes — while parsing continues, and a later smaller
  // value that still fits is captured (see CaptureAllScalarsHandler in
  // parser_config_test.cc). Counts recorded value bytes only; keys are
  // already bounded by the cursor's kMaxKeyBytes.
  // Note the document-order bias: earlier fields win the budget, so a
  // hostile body can front-load junk to crowd out later fields.
  // 0 = no total limit.
  size_t max_total_capture_bytes{0};

  // When true, capture every scalar value in the body (strings, numbers,
  // booleans, nulls) keyed by its leaf dict key ("" for array elements),
  // with no per-field routing: the handler needs no PatternSegment
  // conversion, no matchesPatternPath() calls, and no track_paths cursor
  // mode.
  //
  // Mutually exclusive with extract_fields — exactly one extraction mode may
  // be active; validate() rejects a config that sets both.
  //
  // TODO(tyxia): open design points for this mode — qualified naming for
  // nested scalars (the leaf key alone is ambiguous across parents) and a
  // depth / inside-array scope cutoff.
  bool capture_all_scalars{false};

  // Extraction policy to extract JSON fields from the body.
  //
  // The handler routes extraction by structural matching: convert each spec's
  // segments to WuffsJsonCursor::PatternSegment once at init, then call
  // cursor.matchesPatternPath(segments, depth) at each callback.
  //
  // Mutually exclusive with capture_all_scalars (see above).
  std::vector<ExtractFieldSpec> extract_fields;

  // Returns the minimum cursor max_depth needed to reach all declared fields.
  // Returns 0 when extract_fields is empty (caller should use the cursor default).
  // The caller is responsible for clamping the result to the cursor's
  // compile-time depth bound (WuffsJsonCursor::kMaxTrackedDepth - 1).
  int requiredMaxDepth() const;

  // Checks the inter-field invariants: capture_all_scalars and extract_fields
  // are mutually exclusive extraction modes, so a config that sets both is
  // rejected with InvalidArgumentError.
  absl::Status validate() const;
};

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
