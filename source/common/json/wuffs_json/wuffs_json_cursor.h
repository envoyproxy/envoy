#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

// Wuffs JSON tokenizer — declarations only.
// WUFFS_IMPLEMENTATION is defined in exactly one translation unit: wuffs_impl.c.
#include "release/c/wuffs-v0.4.c"

namespace Envoy {
namespace Json {
namespace Wuffs {

// WuffsJsonCursor — streaming SAX-style JSON parser built on the Wuffs library.
//
// Tokenizes a JSON document delivered as a sequence of byte chunks and fires
// synchronous callbacks into a Handler. Chunks may split at any byte boundary.
//
//   MyHandler h;
//   WuffsJsonCursor cursor(h);
//   for (const Chunk& c : body_chunks) {
//     if (auto s = cursor.feed(c.data, c.is_last); !s.ok()) { /* error */ }
//   }
//
// Every callback receives `depth` (1 = root container) and, for dict values,
// `key` (the dict key to the left; "" for array elements).
//
//   { "a": { "b": [ 1, 2 ] } }
//    ^d=1    ^d=2   ^d=3
//
// Callback sequence for {"messages": [{"role": "user"}]}:
//
//   onContainerOpen    (key="",         is_dict=true,  depth=1)
//   onKey              ("messages",                    depth=1)
//   onContainerOpen    (key="messages", is_dict=false, depth=2)
//   onContainerOpen    (key="",         is_dict=true,  depth=3)
//   onKey              ("role",                        depth=3)
//   openStringCapture  ("role",  depth=3, token_start)  → true/false
//   onStringChunk      ("role",  depth=3, "user")
//   closeStringCapture ("role",  depth=3, token_end)
//   onContainerClose   (depth=3..1, ...)
//
// String capture: return true from openStringCapture to receive decoded UTF-8
// via onStringChunk; false discards at zero cost. onStringChunk returning false
// stops delivery but parsing continues; closeStringCapture always fires.
//
// Container byte ranges: onContainerOpen/onContainerClose deliver token_start/
// token_end forming a half-open [token_start, token_end) over raw body bytes.
//
// Path tracking: with track_paths=true, call matchesPatternPath(segments, depth)
// from any callback for structural spec matching.
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
  //     Always fires, even if openStringCapture returned false.
  //     `token_end` is the byte offset immediately past the closing ".
  //
  //   onKey(key, depth)
  //     Called when a dict key completes. Return a non-OK Status to abort
  //     parsing (e.g. duplicate-key detection).
  //
  //   onNumber(key, raw, depth)
  //     Called for JSON number literals (integer or floating-point).
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
    // no onStringChunk calls occur and no allocation is made — zero cost regardless
    // of string size. closeStringCapture always fires either way.
    //
    // `key`         — dict key immediately left of this value, or "" for array elements.
    // `depth`       — nesting depth of this string value.
    // `token_start` — byte offset of the opening '"' in the body stream.
    virtual bool openStringCapture(absl::string_view key, int depth, size_t token_start) = 0;

    // Called for each decoded UTF-8 content chunk of a non-key string value.
    // Only called when openStringCapture returned true for this string.
    //
    // Chunks deliver fully decoded UTF-8: backslash escapes (e.g., \n, \t, etc.)
    // are converted by the cursor before this call (e.g. \n -> 0x0A).
    // `chunk` is valid only for the duration of this call — do not retain it.
    //
    // Return true to continue receiving chunks. Return false to stop: the cursor
    // delivers no further chunks but continues parsing to find the closing '"';
    // closeStringCapture still fires with token_end.
    virtual bool onStringChunk(absl::string_view key, int depth, absl::string_view chunk) = 0;

    // Called when a string value chain completes. Always fires, even if
    // openStringCapture returned false. `token_end` is the offset past '"'.
    virtual void closeStringCapture(absl::string_view key, int depth, size_t token_end) = 0;

    // Called when a dict key completes. `token_start` is the offset of the
    // opening '"'; paired with the next value callback's token_end it spans
    // the full "key":value field. Return non-OK to abort parsing.
    virtual absl::Status onKey(absl::string_view key, int depth, size_t token_start) = 0;

    // Called for number literals. Return non-OK to abort parsing.
    virtual absl::Status onNumber(absl::string_view key, absl::string_view raw, int depth,
                                  size_t token_start, size_t token_end) = 0;

    // Called for true/false literals. Return non-OK to abort parsing.
    virtual absl::Status onBoolean(absl::string_view key, bool value, int depth, size_t token_start,
                                   size_t token_end) = 0;

    // Called for null literals.
    virtual void onNull(absl::string_view key, int depth, size_t token_start, size_t token_end) = 0;

    // Called after depth has been incremented for a { or [ open. `key` is the
    // parent dict key or "" for array/root. token_start is the offset of { or [.
    //
    // matchesPatternPath note: use depth-1, not depth — key_stack_[depth] is
    // not yet populated at this point. Match an N-segment container spec with
    // matchesPatternPath(segments, depth-1) and compare `key`/is_dict yourself.
    // TODO(tyxia): add matchesContainerPatternPath convenience wrapper.
    virtual void onContainerOpen(absl::string_view key, bool is_dict, int depth,
                                 size_t token_start) = 0;

    // Called with the container's depth before decrement and the offset past } or ].
    virtual void onContainerClose(int depth, size_t token_end) = 0;
  };

  explicit WuffsJsonCursor(Handler& handler, bool track_paths = false);

  WuffsJsonCursor(const WuffsJsonCursor&) = delete;
  WuffsJsonCursor& operator=(const WuffsJsonCursor&) = delete;
  WuffsJsonCursor(WuffsJsonCursor&&) = delete;
  WuffsJsonCursor& operator=(WuffsJsonCursor&&) = delete;

  // Feed one body chunk. Set closed=true on the final chunk (signals EOF to Wuffs).
  // Returns non-OK on malformed JSON or internal allocation failure.
  absl::Status feed(absl::string_view chunk, bool closed);

  // One level of a structural pattern-path match: a dict key or an array wildcard.
  // `key` must outlive the matchesPatternPath() call.
  struct PatternSegment {
    absl::string_view key;
    bool is_array_element{false};
  };

  // True iff the root-to-here chain at `depth` matches `segments` exactly.
  // Dict labels are compared atomically (no serialization, so a document key
  // containing '.', '[', ']' cannot masquerade as nested structure).
  // Zero allocations; O(depth) string_view compares with early exit.
  // Requires track_paths=true at construction; misuse is caught by ASSERT in
  // debug builds. Must be called from within a Handler callback.
  bool matchesPatternPath(absl::Span<const PatternSegment> segments, int depth) const;

  // Monotonically increasing offset of the next source byte to be consumed.
  // Aligns with token_start / token_end values in callbacks.
  size_t nextSourcePosition() const { return body_src_pos_; }

  // Exclusive upper bound for per-depth state tracking (depths 1–8).
  // Covers the deepest known LLM API schema paths (depth 7) plus one buffer.
  // Nesting beyond kMaxTrackedDepth-1 is rejected with InvalidArgumentError.
  // Public so callers can pass kMaxTrackedDepth - 1 to parseExtractFieldSpec.
  static constexpr int kMaxTrackedDepth = 9;

private:
  Handler& handler_;
  // When true, push_key_[d] is updated on every container push so that
  // matchesPatternPath can compare intermediate dict labels. Only needed by
  // the spec-based extraction mode (extract_fields); capture_all_scalars and
  // handlers that route by depth+key alone can leave this false.
  const bool track_paths_;

  wuffs_json__decoder::unique_ptr decoder_;
  static constexpr size_t kTokenBufLen = 256;
  wuffs_base__token token_data_[kTokenBufLen];
  wuffs_base__token_buffer token_buf_{};

  size_t body_src_pos_{0};
  bool wuffs_done_{false};
  // Cap key length at 256 bytes: well above any legitimate schema field name
  // (longest observed ~25B, e.g. "input_audio_transcription") while bounding
  // per-key allocation and guarding against DoS via unbounded key lengths.
  static constexpr size_t kMaxKeyBytes = 256;
  // kMaxPendingBytes is a hard limit against the byte-at-a-time DoS when a NUMBER token is split
  // across chunk boundaries.
  // Note: max_body_bytes (enforced by the outer filter via ParserConfig) also bounds pending_bytes_
  // up to max_body_bytes bytes, kMaxPendingBytes provides a tight cap sized for the largest
  // legitimate number (~25 chars).
  static constexpr size_t kMaxPendingBytes = 64;
  int depth_{0};
  bool is_dict_[kMaxTrackedDepth]{};
  bool expecting_key_[kMaxTrackedDepth]{};

  // key_stack_[d] — current key at depth d; always maintained; forwarded as
  //                 the `key` argument to callbacks.
  // push_key_[d]  — key that opened the container at depth d; maintained only
  //                 when track_paths_=true (used by matchesPatternPath).
  std::string key_stack_[kMaxTrackedDepth]{};
  std::string push_key_[kMaxTrackedDepth + 1]{};
  // duplicate-key detection.
  absl::flat_hash_set<std::string> seen_keys_[kMaxTrackedDepth]{};

  bool in_string_chain_{false};
  // bytes rewound by Wuffs on short_read; prepended to next chunk.
  std::string pending_bytes_;

  // TODO(tyxia): implement the production Handler that accepts ParserConfig and
  // routes callbacks via matchesPatternPath, enforcing the three budget fields.
  bool string_is_key_{false};
  bool string_capturing_{false};    // openStringCapture returned true for current value string
  bool string_chunk_active_{false}; // onStringChunk hasn't returned false yet
  std::string key_buffer_;
  size_t key_token_start_{0};

  absl::Status handleStructureToken(uint64_t token_detail, size_t token_start);
  absl::Status handleStringToken(absl::string_view raw, uint64_t token_detail, bool continued,
                                 size_t token_start);
  absl::Status handleUnicodeCodePointToken(uint64_t token_detail);
  absl::Status handleNumberOrLiteralToken(int64_t token_category, absl::string_view raw,
                                          size_t token_start);
};

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
