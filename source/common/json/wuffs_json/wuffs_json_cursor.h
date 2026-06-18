#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

// Wuffs JSON tokenizer — declarations only.
// WUFFS_IMPLEMENTATION is defined in exactly one translation unit: wuffs_impl.c.
#include "release/c/wuffs-v0.4.c"

namespace Envoy {
namespace Json {
namespace Wuffs {

// WuffsJsonCursor — streaming SAX-style JSON parser built on the Wuffs library
// (https://github.com/google/wuffs).
//
// The cursor tokenizes a JSON document delivered as a sequence of byte chunks
// (e.g. HTTP body fragments arriving on an event loop) and fires synchronous
// callbacks into a Handler as semantic events are recognized. All low-level
// Wuffs mechanics — token ring-buffer management, coroutine suspension and
// resumption, escape sequence decoding — are hidden behind the Handler
// interface; consumers see only clean, decoded events.
//
// Streaming model
//
// Call feed(chunk, closed) for each incoming chunk; set closed=true on the
// final one to signal EOF. The Wuffs decoder is a class member that preserves
// all parse state across calls, so chunks may split at any byte boundary —
// including the middle of a string value or a multi-digit number:
//
//   class MyHandler : public WuffsJsonCursor::Handler { ... };
//   MyHandler h;
//   WuffsJsonCursor cursor(h);
//   for (const Chunk& c : http_body_chunks) {
//     if (auto s = cursor.feed(c.data, c.is_last); !s.ok()) { /* error */ }
//   }
//
// Depth and key model
//
// Every callback receives `depth` — the nesting level of the value or container
// being reported. Depth starts at 0 before the root container.
// onContainerOpen fires after depth is incremented; onContainerClose fires
// before it is decremented, so both report the depth of the container itself.
//
//   { "a": { "b": [ 1, 2 ] } }
//    ^d=1    ^d=2   ^d=3
//
// For values inside a dict, callbacks also receive `key` — the dict key
// immediately to the left of the value. For array elements, key is "".
// The cursor tracks the current key internally, so handlers never need a
// separate current_key_ member.
//
// Example callback sequence for {"messages": [{"role": "user"}]}:
//
//   onContainerOpen    (key="",         is_dict=true,  depth=1)  ← root {
//   onKey              ("messages",                    depth=1)
//   onContainerOpen    (key="messages", is_dict=false, depth=2)  ← [
//   onContainerOpen    (key="",         is_dict=true,  depth=3)  ← { (parent is array)
//   onKey              ("role",                        depth=3)
//   openStringCapture  ("role",  depth=3, token_start)  → true/false
//   onStringChunk      ("role",  depth=3, "user")        ← if true; one call per chunk
//   closeStringCapture ("role",  depth=3, token_end)     ← if true
//   onContainerClose   (depth=3, ...)
//   onContainerClose   (depth=2, ...)
//   onContainerClose   (depth=1, ...)
//
// String value lifecycle
//
// At the start of a string value the cursor calls openStringCapture. Return
// true to receive the content as decoded UTF-8 via onStringChunk — one call
// per chunk across however many Wuffs tokens or feed() calls the string spans.
// When the closing quote is seen, closeStringCapture fires with token_end.
// Return false to discard at zero cost: no onStringChunk calls occur, no
// allocation is made, and closeStringCapture is never called.
//
// onStringChunk delivers fully decoded UTF-8; backslash escapes are converted
// by the cursor before the call. Each chunk is a string_view valid only for
// the duration of that call — copy if retention is needed.
//
// Fine-grained control inside large values
//
// onStringChunk returning bool enables chunk-level decisions within a single
// string value, not just the all-or-nothing choice at openStringCapture:
//
//   Partial capture — accumulate up to a limit, then stop appending but keep
//   returning true so the cursor continues parsing to the closing ":
//     if (buf_.size() < kLimit) { buf_.append(chunk); }
//     return true;
//
//   Early abort — stop chunk delivery entirely once a threshold is reached:
//     return buf_.size() < kLimit;  // false stops further onStringChunk calls
//
//   Streaming without accumulation — process each chunk in place (e.g. hash,
//   scan for keywords) without ever buffering the full value:
//     hasher_.Update(chunk);
//     return true;
//
// In all cases closeStringCapture still fires with token_end, so byte-range
// information is available regardless of how the content was handled.
//
// For large values (e.g. message content) where decoding is unnecessary,
// return false from openStringCapture and use onContainerOpen/Close byte
// ranges on the parent container for zero-copy access to the raw JSON bytes.
//
// Container byte ranges
//
// onContainerOpen receives token_start — the byte offset of the opening { or [
// in the original body stream. onContainerClose receives token_end — the offset
// immediately past the closing } or ]. Together they delimit a half-open byte
// range [token_start, token_end) suitable for zero-copy sub-range extraction when
// the body is memory-mapped or stored contiguously.
//
// Path tracking
//
// Construct with track_paths=true and call buildIndexedPath(depth) or
// buildPatternPath(depth) from within any callback to obtain dot-notation
// path strings for the current position:
//   buildIndexedPath → "messages[0].role"  — concrete index, for per-element keys
//   buildPatternPath → "messages[].role"   — wildcard index, for config matching
//
class WuffsJsonCursor {
public:
  // Handler — callback interface implemented by the JSON document consumer.
  //
  // All callbacks are invoked synchronously from within feed().
  //
  //   openStringCapture(key, depth, token_start)
  //     Called at the start of every non-key string value chain.
  //     Return true to receive decoded content via onStringChunk, or false to
  //     discard — no onStringChunk calls, no allocation, zero cost.
  //     `key` is the dict key for this value, or "" for array elements.
  //     `token_start` is the byte offset of the opening " in the body stream.
  //
  //   onStringChunk(key, depth, chunk)
  //     Called for each decoded UTF-8 content chunk of a non-key string value.
  //     `chunk` is valid only for the duration of this call; do not retain it.
  //     Return true to keep receiving chunks, false to stop. If false, the
  //     cursor stops delivering chunks but continues parsing to find the closing
  //     "; closeStringCapture still fires.
  //
  //   closeStringCapture(key, depth, token_end)
  //     Called when a non-key string chain completes (closing " seen).
  //     Only fires if openStringCapture returned true for this string.
  //     `token_end` is the byte offset immediately past the closing ".
  //
  //   onKey(key, depth)
  //     Called when a dict key completes. Return a non-OK Status to abort
  //     parsing (e.g. duplicate-key detection).
  //
  //   onNumber(key, raw, depth)
  //     Called for JSON number literals (integer or floating-point).
  //     `raw` is the source bytes; parse with absl::SimpleAtoi / SimpleAtod.
  //     Return a non-OK Status to abort parsing.
  //
  //   onBoolean(key, value, depth)
  //     Called for JSON true / false literals.
  //     Return a non-OK Status to abort parsing.
  //
  //   onNull(key, depth)
  //     Called for JSON null literals.
  //
  //   onContainerOpen(key, is_dict, depth, token_start)
  //     Called after depth has been incremented for a { or [ open.
  //     `key` is the parent dict key that opened this container, or "" if the
  //     parent is an array or this is the root container.
  //     `token_start` is the byte offset of the opening delimiter in the original
  //     body stream, useful for byte-range recording.
  //
  //   onContainerClose(depth, token_end)
  //     Called with the container's depth before decrement and the byte offset
  //     immediately after the closing } or ].
  class Handler {
  public:
    virtual ~Handler() = default;
    // Called once at the opening '"' of every non-key string value chain.
    // This is the routing decision point: the handler inspects `key` and `depth`
    // and decides whether to capture this value.
    //
    // Return true to receive content via onStringChunk. Return false to discard:
    // no onStringChunk calls occur, no allocation is made, and closeStringCapture
    // is never called — zero cost regardless of string size.
    //
    // `key`         — dict key immediately left of this value, or "" for array elements.
    // `depth`       — nesting depth of this string value.
    // `token_start` — byte offset of the opening '"' in the body stream.
    virtual bool openStringCapture(absl::string_view key, int depth, size_t token_start) = 0;

    // Called for each decoded UTF-8 content chunk of a non-key string value.
    // Only called when openStringCapture returned true for this string.
    //
    // Chunks deliver fully decoded UTF-8: backslash escapes (\n, \uXXXX, etc.)
    // are converted by the cursor before this call (e.g. \n -> 0x0A).
    // `chunk` is valid only for the duration of this call — do not retain it.
    //
    // Return true to continue receiving chunks. Return false to stop: the cursor
    // delivers no further chunks but continues parsing to find the closing '"';
    // closeStringCapture still fires with token_end.
    virtual bool onStringChunk(absl::string_view key, int depth, absl::string_view chunk) = 0;

    // Called when a non-key string value chain completes (closing '"' seen).
    // Only fires if openStringCapture returned true for this string.
    // `token_end` is the byte offset immediately past the closing '"'.
    virtual void closeStringCapture(absl::string_view key, int depth, size_t token_end) = 0;
    // `token_start` is the byte offset of the opening '"' of the key in the body
    // stream. Combined with the token_end delivered by the subsequent value
    // callback (closeStringCapture, onNumber, onBoolean, onNull, or
    // onContainerClose), it gives the half-open byte range [token_start, token_end)
    // covering the complete "key":value field — suitable for verbatim passthrough
    // without DOM parsing.
    virtual absl::Status onKey(absl::string_view key, int depth, size_t token_start) = 0;
    // JSON scalar types: number, boolean (true/false), and null.
    // `token_start` / `token_end` delimit the scalar value token in the body stream.
    // Together with the token_start from the preceding onKey call they cover the
    // complete "key":value field byte range.
    virtual absl::Status onNumber(absl::string_view key, absl::string_view raw, int depth,
                                  size_t token_start, size_t token_end) = 0;
    virtual absl::Status onBoolean(absl::string_view key, bool value, int depth, size_t token_start,
                                   size_t token_end) = 0;
    virtual void onNull(absl::string_view key, int depth, size_t token_start, size_t token_end) = 0;
    // Called after depth has been incremented for a { or [ open.
    // `key` is the parent dict key that opened this container, or "" when
    // the parent is an array or this is the root container.
    // `token_start` is the byte offset of the opening { or [ in the body stream.
    //
    // PATH TRACKING NOTE — call buildPatternPath(depth-1), NOT buildPatternPath(depth):
    // At the time this callback fires, key_stack_[depth] is not yet populated
    // (no keys have been seen inside the new container). buildPatternPath(depth-1)
    // gives the enclosing container's path, which combined with `key` identifies
    // this container unambiguously.
    // Example: onContainerOpen(key="parameters", is_dict=true, depth=5)
    //   buildPatternPath(4) → "tools[].function.parameters"  ← correct
    //   buildPatternPath(5) → "tools[].function.<stale>"     ← wrong
    // TODO(tyxia): add buildContainerPatternPath(key, is_dict, depth)
    //   convenience wrapper that hides this depth-1 subtlety.
    virtual void onContainerOpen(absl::string_view key, bool is_dict, int depth,
                                 size_t token_start) = 0;
    virtual void onContainerClose(int depth, size_t token_end) = 0;
  };

  // max_depth: maximum nesting depth allowed before feed() returns
  // InvalidArgumentError. Default (kMaxTrackedDepth-1 = 8) covers all known
  // OpenAI/Anthropic schema paths. Pass a larger value to accept deeper JSON,
  // but note that key/dup/path tracking is only accurate to kMaxTrackedDepth-1
  // regardless of max_depth — see TODO below.
  explicit WuffsJsonCursor(Handler& handler, bool track_paths = false,
                           int max_depth = kMaxTrackedDepth - 1);

  // Feed one body chunk. Set closed=true on the final chunk (signals EOF to Wuffs).
  // Returns non-OK on malformed JSON or internal allocation failure.
  absl::Status feed(absl::string_view chunk, bool closed);

  // Build dot-notation path strings for the field currently being selected.
  // Return a dot-notation path string for the current cursor position.
  // Must only be called from within a Handler callback while feed() is active.
  // Requires track_paths=true at construction.
  std::string buildIndexedPath(int depth) const; // e.g. "messages[0].role"
  std::string buildPatternPath(int depth) const; // e.g. "messages[].role"

  // Monotonically increasing byte offset of the next source byte to be consumed.
  // Matches the token_start / token_end values delivered to onContainerOpen / onContainerClose.
  // TODO(tyxia): the outer filter should compare nextSourcePosition() against
  //   DecoderConfig::max_body_bytes between feed() calls and return ResourceExhausted
  //   before calling feed() again if the limit is exceeded.
  size_t nextSourcePosition() const { return body_src_pos_; }

private:
  Handler& handler_;
  bool track_paths_;
  int max_depth_;

  wuffs_json__decoder::unique_ptr decoder_;
  static constexpr size_t kTokenBufLen = 256;
  wuffs_base__token token_data_[kTokenBufLen];
  wuffs_base__token_buffer token_buf_{};

  size_t body_src_pos_{0};
  bool wuffs_done_{false};

  // Exclusive upper bound for per-depth state tracking: depths 1 through
  // kMaxTrackedDepth-1 (currently 1–8) have full key/dup/path tracking.
  // Value covers the deepest known OpenAI/Anthropic schema paths:
  //   tools[i].function.parameters.properties.<arg>.type  (depth 7)
  //   messages[i].content[j].content[k].text              (depth 7)
  // plus one buffer level for schemas with one extra level of nesting.
  //
  // Nesting beyond kMaxTrackedDepth-1 is rejected by default (see max_depth
  // constructor argument). Key/dup/path tracking accuracy is bounded by
  // kMaxTrackedDepth-1 regardless of max_depth, because the per-depth arrays
  // below are stack-allocated at compile time.
  //
  // TODO(tyxia): replace the fixed arrays with std::vector<T> so
  // that max_depth_ can exceed kMaxTrackedDepth-1 without losing tracking
  // accuracy. This removes the hard compile-time cap at the cost of per-push
  // heap allocation; evaluate against the request-path perf budget before
  // doing so.
  static constexpr int kMaxTrackedDepth = 9;
  static constexpr size_t kMaxKeyBytes = 256;
  int depth_{0};
  bool is_dict_[kMaxTrackedDepth]{};
  bool expecting_key_[kMaxTrackedDepth]{};

  // key_stack_[d]   — most recently completed key at dict depth d.
  //                   Always maintained (not gated on track_paths_) because it
  //                   is forwarded as the `key` argument to Handler callbacks.
  // push_key_[d]    — key at depth d-1 that opened the container at depth d;
  //                   captured at push time. Size kMaxTrackedDepth+1 so
  //                   push_key_[kMaxTrackedDepth] is accessible when depth_
  //                   reaches kMaxTrackedDepth.
  //                   Only maintained when track_paths_=true (used by
  //                   buildIndexedPath/buildPatternPath).
  // array_index_[d] — count of elements already completed at array depth d;
  //                   reset to 0 on container open, incremented after each
  //                   completed element (container close, scalar, or string value)
  //                   when the enclosing container is an array.
  //                   Used by buildIndexedPath (requires track_paths_=true).
  std::string key_stack_[kMaxTrackedDepth]{};
  std::string push_key_[kMaxTrackedDepth + 1]{};
  int array_index_[kMaxTrackedDepth]{};
  // Tracks keys seen at each dict depth to detect and reject duplicates.
  // Cleared on container open; flat_hash_set gives O(1) insert/lookup with
  // one contiguous backing allocation (no per-node malloc unlike std::set).
  absl::flat_hash_set<std::string> seen_keys_[kMaxTrackedDepth]{};

  bool in_string_chain_{false};

  // Bytes unread by Wuffs before the last short_read suspension. Wuffs rewinds
  // iop_a_src to before an incomplete NUMBER or LITERAL token before suspending,
  // so those bytes must be prepended to the next chunk to form a contiguous
  // buffer. Empty between feed() calls when no token straddles a boundary.
  std::string pending_bytes_;

  // TODO(tyxia): Implement PolicyHandler : Handler that accepts
  // DecoderConfig (max_body_bytes, max_inline_bytes, max_element_capture_bytes)
  // and a list of ExtractFieldSpec; routes callbacks by matching
  // buildPatternPath(depth) / buildPatternPath(depth-1) at onContainerOpen
  // against specs and records element byte ranges. Implements the three-tier
  // body-size logic (full capture / semantic-only / reject).
  // Implement a utility to parse ExtractFieldSpec path
  // strings ("messages[].content[].text") into a normalized form directly
  // comparable with buildPatternPath() output.
  // At filter init, derive the max_depth constructor
  // argument from the deepest ExtractFieldSpec path rather than the static
  // default, tightening the DoS bound to exactly what the policy requires.
  bool string_is_key_{false};
  bool string_capturing_{false};    // openStringCapture returned true for current value string
  bool string_chunk_active_{false}; // onStringChunk hasn't returned false yet
  std::string key_buffer_;
  size_t key_token_start_{0};
};

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
