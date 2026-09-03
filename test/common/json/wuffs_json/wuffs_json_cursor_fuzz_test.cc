#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/json/wuffs_json/parser_config.h"
#include "source/common/json/wuffs_json/wuffs_json_cursor.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Json {
namespace Wuffs {
namespace {

using PatternSegment = WuffsJsonCursor::PatternSegment;

// Larger bodies cost parse time without reaching new states: chunk-boundary
// stitching, the depth cap and the key-length cap are all reachable well below
// this, and each body is re-parsed byte-at-a-time below.
constexpr size_t kMaxInputSize = 4 * 1024;

// Longest prefix of the body handed to the extraction path parser. Real paths
// name a handful of schema fields, so a longer prefix only adds parse time.
constexpr size_t kMaxPathBytes = 256;

// A pattern deeper than the cursor tracks, to keep the per-depth array indexing
// in matchesChain() under test.
std::vector<PatternSegment> tooDeepPattern() {
  std::vector<PatternSegment> segments;
  for (int i = 0; i < WuffsJsonCursor::kMaxTrackedDepth + 4; ++i) {
    segments.push_back({absl::StrCat("k", i), /*is_array_element=*/false});
  }
  return segments;
}

// Pattern paths the cursor is asked to match at every callback. Matching is a
// pure query over cursor state, so its result is part of the chunk-invariant
// trace below.
const std::vector<std::vector<PatternSegment>>& patternTable() {
  CONSTRUCT_ON_FIRST_USE(
      std::vector<std::vector<PatternSegment>>, std::vector<PatternSegment>{},
      std::vector<PatternSegment>{{"model", false}},
      std::vector<PatternSegment>{{"messages", false}, {"", true}, {"role", false}},
      std::vector<PatternSegment>{
          {"tools", false}, {"", true}, {"function", false}, {"name", false}},
      tooDeepPattern());
}

// Picks the pattern for this input. Any stable spread over the table works;
// libFuzzer covers the rest of the table through the corpus.
size_t fnv1a(absl::string_view data) {
  size_t hash = 0xcbf29ce484222325ULL;
  for (const char c : data) {
    hash = (hash ^ static_cast<uint8_t>(c)) * 0x100000001b3ULL;
  }
  return hash;
}

// How the handler pushes back on the cursor. Every decision is a pure function
// of the callback arguments and of the callback ordinal, never of how the body
// was chunked.
struct HandlerBehavior {
  // Stop string delivery at the first chunk of every captured value. This makes
  // the recorded value chunk-dependent, so callers that set it must not compare
  // traces.
  bool stop_string_chunks{false};
  // 1-based ordinal of the status-returning callback that aborts the parse;
  // 0 aborts nothing.
  uint32_t abort_at{0};
};

// Records one trace line per callback.
//
// The trace is invariant under chunking: a chunk boundary inside a string value
// changes how many onStringChunk calls carry it, but not the decoded bytes, so
// content is accumulated and emitted once at closeStringCapture.
class TracingHandler : public WuffsJsonCursor::Handler {
public:
  TracingHandler(absl::Span<const PatternSegment> pattern, HandlerBehavior behavior)
      : pattern_(pattern), behavior_(behavior) {}

  // The cursor takes its handler by reference, so the back-pointer used for the
  // match queries is wired up once both objects exist.
  void setCursor(const WuffsJsonCursor& cursor) { cursor_ = &cursor; }

  const std::string& trace() const { return trace_; }

  bool openStringCapture(absl::string_view key, int depth, size_t token_start) override {
    absl::StrAppend(&trace_, "open_str|", absl::CEscape(key), "|", depth, "|", token_start, "\n");
    value_.clear();
    // Capture at odd depths only, so the zero-cost discard path is exercised
    // too. Depth is chunk-independent, so the choice is stable across passes.
    capturing_ = (depth % 2) == 1;
    return capturing_;
  }

  bool onStringChunk(absl::string_view /*key*/, int /*depth*/, absl::string_view chunk) override {
    value_.append(chunk.data(), chunk.size());
    return !behavior_.stop_string_chunks;
  }

  void closeStringCapture(absl::string_view key, int depth, size_t token_end) override {
    absl::StrAppend(&trace_, "close_str|", absl::CEscape(key), "|", depth, "|", token_end, "|",
                    matchesValue(), "|");
    // A truncated value depends on where the chunk fell, so leave it out.
    if (capturing_ && !behavior_.stop_string_chunks) {
      absl::StrAppend(&trace_, absl::CEscape(value_));
    }
    absl::StrAppend(&trace_, "\n");
    capturing_ = false;
  }

  absl::Status onKey(absl::string_view key, int depth, size_t token_start) override {
    absl::StrAppend(&trace_, "key|", absl::CEscape(key), "|", depth, "|", token_start, "\n");
    return abortIfDue();
  }

  absl::Status onNumber(absl::string_view key, absl::string_view raw, int depth, size_t token_start,
                        size_t token_end) override {
    absl::StrAppend(&trace_, "num|", absl::CEscape(key), "|", absl::CEscape(raw), "|", depth, "|",
                    token_start, "|", token_end, "|", matchesValue(), "\n");
    return abortIfDue();
  }

  absl::Status onBoolean(absl::string_view key, bool value, int depth, size_t token_start,
                         size_t token_end) override {
    absl::StrAppend(&trace_, "bool|", absl::CEscape(key), "|", value, "|", depth, "|", token_start,
                    "|", token_end, "|", matchesValue(), "\n");
    return abortIfDue();
  }

  void onNull(absl::string_view key, int depth, size_t token_start, size_t token_end) override {
    absl::StrAppend(&trace_, "null|", absl::CEscape(key), "|", depth, "|", token_start, "|",
                    token_end, "|", matchesValue(), "\n");
  }

  void onContainerOpen(absl::string_view key, bool is_dict, int depth,
                       size_t token_start) override {
    absl::StrAppend(&trace_, "open|", absl::CEscape(key), "|", is_dict, "|", depth, "|",
                    token_start, "|", cursor_->matchesContainerPatternPath(pattern_), "\n");
  }

  void onContainerClose(int depth, size_t token_end) override {
    absl::StrAppend(&trace_, "close|", depth, "|", token_end, "\n");
  }

private:
  bool matchesValue() const { return cursor_->matchesPatternPath(pattern_); }

  absl::Status abortIfDue() {
    ++callback_count_;
    if (behavior_.abort_at != 0 && callback_count_ == behavior_.abort_at) {
      return absl::InternalError("fuzz handler abort");
    }
    return absl::OkStatus();
  }

  const absl::Span<const PatternSegment> pattern_;
  const HandlerBehavior behavior_;
  const WuffsJsonCursor* cursor_{nullptr};
  std::string trace_;
  std::string value_;
  bool capturing_{false};
  uint32_t callback_count_{0};
};

struct PassResult {
  absl::Status status;
  std::string trace;
};

// Feeds `body` in fixed-size chunks, marking the final chunk closed. A
// chunk_size at or above the body size is a single-shot feed.
PassResult runPass(absl::string_view body, size_t chunk_size,
                   absl::Span<const PatternSegment> pattern, HandlerBehavior behavior) {
  TracingHandler handler(pattern, behavior);
  WuffsJsonCursor cursor(handler);
  handler.setCursor(cursor);

  absl::Status status = absl::OkStatus();
  size_t offset = 0;
  do {
    const size_t take = std::min(chunk_size, body.size() - offset);
    status = cursor.feed(body.substr(offset, take), /*closed=*/offset + take == body.size());
    offset += take;
  } while (status.ok() && offset < body.size());
  return {std::move(status), handler.trace()};
}

// Exercises the extraction path parser, which is hand-written and reachable
// from operator configuration.
void fuzzExtractFieldSpec(absl::string_view body) {
  const absl::string_view path = body.substr(0, std::min(body.size(), kMaxPathBytes));
  // Unbounded depth may yield more segments than the cursor tracks; bounded
  // depth is what production passes.
  parseExtractFieldSpec(path, /*max_depth=*/0).IgnoreError();

  ParserConfig config;
  config.capture_all_scalars = true;
  if (absl::StatusOr<ExtractFieldSpec> spec =
          parseExtractFieldSpec(path, WuffsJsonCursor::kMaxTrackedDepth - 1);
      spec.ok()) {
    config.extract_fields.push_back(*std::move(spec));
  }
  config.validate().IgnoreError();
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  if (len > kMaxInputSize) {
    return;
  }
  const absl::string_view body(reinterpret_cast<const char*>(buf), len);
  fuzzExtractFieldSpec(body);

  // One pattern per input rather than all of them, to keep the per-input parse
  // count low; the corpus covers the table.
  const std::vector<std::vector<PatternSegment>>& patterns = patternTable();
  const std::vector<PatternSegment>& pattern = patterns[fnv1a(body) % patterns.size()];

  // Reference pass: the whole body in one feed.
  const PassResult reference = runPass(body, body.size(), pattern, HandlerBehavior{});

  // A chunk boundary may fall anywhere in a streamed body, so the cursor has to
  // stitch split tokens back together and report byte offsets in the global body
  // space. Re-feeding the same bytes in smaller chunks must therefore produce the
  // same callbacks. Byte-at-a-time covers every boundary; the odd sizes land
  // boundaries at other offsets within multi-byte tokens.
  for (const size_t chunk_size : {size_t{1}, size_t{3}, size_t{7}}) {
    const PassResult chunked = runPass(body, chunk_size, pattern, HandlerBehavior{});
    // A body that parses only one way is not a mismatch: splitting a number
    // across chunks routes it through the pending-bytes cap, which rejects
    // over-long numbers that a single feed accepts.
    if (reference.status.ok() && chunked.status.ok()) {
      RELEASE_ASSERT(reference.trace == chunked.trace,
                     absl::StrCat("chunked parse diverged at chunk size ", chunk_size));
    }
  }

  // Handler push-back: aborting mid-parse and dropping a value part-way must not
  // leave the cursor reading stale state on the next token. Traces are not
  // comparable here, so this pass only guards against crashes.
  const HandlerBehavior push_back{/*stop_string_chunks=*/true,
                                  /*abort_at=*/static_cast<uint32_t>(1 + (len % 8))};
  runPass(body, /*chunk_size=*/2, pattern, push_back);
}

} // namespace
} // namespace Wuffs
} // namespace Json
} // namespace Envoy
