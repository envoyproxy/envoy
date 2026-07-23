#include <string>
#include <utility>
#include <vector>

#include "source/common/json/wuffs_json/parser_config.h"
#include "source/common/json/wuffs_json/wuffs_json_cursor.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace Wuffs {
namespace {

class MockHandler : public WuffsJsonCursor::Handler {
public:
  bool openStringCapture(absl::string_view, int, size_t) override { return false; }
  bool onStringChunk(absl::string_view, int, absl::string_view) override { return true; }
  void closeStringCapture(absl::string_view, int, size_t) override {}
  absl::Status onKey(absl::string_view, int, size_t) override { return absl::OkStatus(); }
  absl::Status onNumber(absl::string_view, absl::string_view, int, size_t, size_t) override {
    return absl::OkStatus();
  }
  absl::Status onBoolean(absl::string_view, bool, int, size_t, size_t) override {
    return absl::OkStatus();
  }
  void onNull(absl::string_view, int, size_t, size_t) override {}
  void onContainerOpen(absl::string_view, bool, int, size_t) override {}
  void onContainerClose(int, size_t) override {}
};

// ============================================================================
// capture_all_scalars mode
// ============================================================================

// CaptureAllScalarsHandler is the executable template for capture-all mode:
// every scalar value is recorded keyed by its leaf dict key ("" for array
// elements) — no PatternSegment conversion, no matchesPatternPath() calls,
// and no track_paths cursor mode. Each record is "key=value;".
class CaptureAllScalarsHandler : public MockHandler {
public:
  explicit CaptureAllScalarsHandler(const ParserConfig& config = {})
      : max_scalar_capture_bytes_(config.max_scalar_capture_bytes),
        max_total_capture_bytes_(config.max_total_capture_bytes) {}

  bool openStringCapture(absl::string_view, int, size_t) override { return true; }
  bool onStringChunk(absl::string_view, int, absl::string_view chunk) override {
    const size_t projected = pending_.size() + chunk.size();
    if ((max_scalar_capture_bytes_ > 0 && projected > max_scalar_capture_bytes_) ||
        (max_total_capture_bytes_ > 0 && total_captured_ + projected > max_total_capture_bytes_)) {
      over_budget_ = true;
      pending_.clear();
      return false; // reject this value — no further chunks, no record
    }
    pending_.append(chunk.data(), chunk.size());
    return true;
  }
  void closeStringCapture(absl::string_view key, int, size_t) override {
    if (over_budget_) {
      over_budget_ = false;
      return;
    }
    record(key, pending_);
    pending_.clear();
  }
  absl::Status onNumber(absl::string_view key, absl::string_view raw, int, size_t,
                        size_t) override {
    record(key, raw);
    return absl::OkStatus();
  }
  absl::Status onBoolean(absl::string_view key, bool value, int, size_t, size_t) override {
    record(key, value ? "true" : "false");
    return absl::OkStatus();
  }
  void onNull(absl::string_view key, int, size_t, size_t) override { record(key, "null"); }

  const std::string& captured() const { return captured_; }

private:
  void record(absl::string_view key, absl::string_view value) {
    if (max_total_capture_bytes_ > 0 && total_captured_ + value.size() > max_total_capture_bytes_) {
      return; // reject: recording would exceed the body-wide budget
    }
    total_captured_ += value.size();
    captured_.append(key.data(), key.size());
    captured_ += '=';
    captured_.append(value.data(), value.size());
    captured_ += ';';
  }

  const size_t max_scalar_capture_bytes_; // 0 = no per-value limit
  const size_t max_total_capture_bytes_;  // 0 = no body-wide limit
  size_t total_captured_{0};              // value bytes recorded so far
  bool over_budget_{false};               // current string value exceeded a budget
  std::string pending_;
  std::string captured_;
};

// All four scalar types are captured across nesting levels, in document
// order, without any spec machinery or path tracking.
TEST(CaptureAllScalarsTest, CapturesEveryScalarWithoutPathTracking) {
  CaptureAllScalarsHandler h;
  WuffsJsonCursor cursor(h); // track_paths not needed in capture-all mode
  constexpr absl::string_view json = R"({"model":"m","max_tokens":5,"stream":false,)"
                                     R"("id":null,"params":{"name":"n"}})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "model=m;max_tokens=5;stream=false;id=null;name=n;");
}

// Array-element scalars arrive with key "" — the leaf-key label carries no
// parent context (the qualified-naming design point tracked in the
// capture_all_scalars TODO).
TEST(CaptureAllScalarsTest, ArrayElementScalarsHaveEmptyKey) {
  CaptureAllScalarsHandler h;
  WuffsJsonCursor cursor(h);
  constexpr absl::string_view json = R"({"stop":["a","b"],"messages":[{"role":"user"}]})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "=a;=b;role=user;");
}

// Capture-all is chunk-boundary safe: a string value split across feed()
// calls is reassembled before the closeStringCapture record.
TEST(CaptureAllScalarsTest, StringValueSplitAcrossChunks) {
  CaptureAllScalarsHandler h;
  WuffsJsonCursor cursor(h);
  ASSERT_TRUE(cursor.feed(R"({"model":"gpt-)", /*closed=*/false).ok());
  ASSERT_TRUE(cursor.feed(R"(4o","n":2})", /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "model=gpt-4o;n=2;");
}

// max_scalar_capture_bytes rejects each over-budget string value independently: the
// oversized value is dropped entirely (no truncated record), parsing
// continues past it, and later values within budget are captured in full.
// A value exactly at the budget is still captured.
TEST(CaptureAllScalarsTest, PerValueBudgetRejectsOversizedStrings) {
  ParserConfig cfg;
  cfg.capture_all_scalars = true;
  cfg.max_scalar_capture_bytes = 4;
  ASSERT_TRUE(cfg.validate().ok());

  CaptureAllScalarsHandler h(cfg);
  WuffsJsonCursor cursor(h);
  constexpr absl::string_view json = R"({"model":"abcdefgh","n":1,"k":"wxyz"})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "n=1;k=wxyz;");
}

// The budget applies to the accumulated decoded size across chunk-split
// deliveries, not per onStringChunk call: 3 bytes from the first feed plus 5
// from the second exceed a 4-byte budget, so the value is rejected mid-chain
// and the partial prefix already accumulated is discarded.
TEST(CaptureAllScalarsTest, PerValueBudgetSpansChunkBoundaries) {
  ParserConfig cfg;
  cfg.capture_all_scalars = true;
  cfg.max_scalar_capture_bytes = 4;

  CaptureAllScalarsHandler h(cfg);
  WuffsJsonCursor cursor(h);
  ASSERT_TRUE(cursor.feed(R"({"model":"abc)", /*closed=*/false).ok());
  ASSERT_TRUE(cursor.feed(R"(defgh","n":1})", /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "n=1;");
}

// max_total_capture_bytes bounds the sum of recorded value bytes across the
// body. A value that would overflow the total is rejected without consuming
// any budget, so a later smaller value that still fits is captured — the
// budget is not a hard stop at the first overflow.
TEST(CaptureAllScalarsTest, TotalBudgetDropsValuesThatDoNotFit) {
  ParserConfig cfg;
  cfg.capture_all_scalars = true;
  cfg.max_total_capture_bytes = 9;
  ASSERT_TRUE(cfg.validate().ok());

  CaptureAllScalarsHandler h(cfg);
  WuffsJsonCursor cursor(h);
  // "abcd" = 4 (total 4); "efghij" = 6 would make 10 > 9, rejected;
  // "xy" = 2 fits the remaining 5 (total 6).
  constexpr absl::string_view json = R"({"a":"abcd","b":"efghij","c":"xy"})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "a=abcd;c=xy;");
}

// The total budget applies to every scalar type, not just strings: number,
// boolean, and null records are counted and rejected by the same rule.
TEST(CaptureAllScalarsTest, TotalBudgetCountsNonStringScalars) {
  ParserConfig cfg;
  cfg.capture_all_scalars = true;
  cfg.max_total_capture_bytes = 5;

  CaptureAllScalarsHandler h(cfg);
  WuffsJsonCursor cursor(h);
  // "12" = 2 (total 2); "true" = 4 would make 6 > 5, rejected;
  // "1" = 1 fits (total 3); "null" = 4 would make 7 > 5, rejected.
  constexpr absl::string_view json = R"({"n":12,"b":true,"x":1,"z":null})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "n=12;x=1;");
}

// A string is rejected against the total budget mid-chain — as soon as the
// accumulating value can no longer fit the remaining budget, chunk delivery
// stops and the partial prefix is discarded without counting against the
// total.
TEST(CaptureAllScalarsTest, TotalBudgetRejectsMidChain) {
  ParserConfig cfg;
  cfg.capture_all_scalars = true;
  cfg.max_total_capture_bytes = 4;

  CaptureAllScalarsHandler h(cfg);
  WuffsJsonCursor cursor(h);
  // "ab" accumulates (2 ≤ 4); the next chunk projects 2+4 = 6 > 4, so the
  // value is rejected mid-chain; "1" then fits the untouched budget.
  ASSERT_TRUE(cursor.feed(R"({"k":"ab)", /*closed=*/false).ok());
  ASSERT_TRUE(cursor.feed(R"(cdef","n":1})", /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "n=1;");
}

// Per-value and total budgets compose: each value must clear both gates.
// "abc" passes the per-value cap but pushes the total over; "de" clears both.
TEST(CaptureAllScalarsTest, PerValueAndTotalBudgetsCompose) {
  ParserConfig cfg;
  cfg.capture_all_scalars = true;
  cfg.max_scalar_capture_bytes = 3;
  cfg.max_total_capture_bytes = 4;

  CaptureAllScalarsHandler h(cfg);
  WuffsJsonCursor cursor(h);
  // "wxyz" = 4 > 3 per-value, rejected; "abc" = 3 ≤ 3 per-value and fits the
  // total (3 ≤ 4); "de" = 2 would make 5 > 4 total, rejected; "q" = 1 fits.
  constexpr absl::string_view json = R"({"a":"wxyz","b":"abc","c":"de","d":"q"})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "b=abc;d=q;");
}

// ============================================================================
// ParserConfig::max_body_bytes contract
// ============================================================================

// max_body_bytes directs the outer filter to check
// cursor.nextSourcePosition() + chunk.size() against the limit before each
// feed() call. These tests pin the two nextSourcePosition() properties that
// contract depends on.

// Discards all values; these tests only observe nextSourcePosition().
// nextSourcePosition() is a global counter over the whole body stream, not
// per-chunk: it accumulates across feed() calls and ends at total body size.
TEST(MaxBodyBytesContractTest, NextSourcePositionAccumulatesAcrossChunks) {
  MockHandler h;
  WuffsJsonCursor cursor(h);
  constexpr absl::string_view part1 = R"({"a":true,)";
  constexpr absl::string_view part2 = R"("b":"xy",)";
  constexpr absl::string_view part3 = R"("c":null})";
  ASSERT_TRUE(cursor.feed(part1, /*closed=*/false).ok());
  EXPECT_EQ(cursor.nextSourcePosition(), part1.size());
  ASSERT_TRUE(cursor.feed(part2, /*closed=*/false).ok());
  EXPECT_EQ(cursor.nextSourcePosition(), part1.size() + part2.size());
  ASSERT_TRUE(cursor.feed(part3, /*closed=*/true).ok());
  EXPECT_EQ(cursor.nextSourcePosition(), part1.size() + part2.size() + part3.size());
}

// nextSourcePosition() lags bytes actually fed by the in-flight token tail
// held in pending_bytes_ (bounded by kMaxPendingBytes) — the slack the
// max_body_bytes pre-feed check must tolerate.
TEST(MaxBodyBytesContractTest, NextSourcePositionLagsByInFlightTokenTail) {
  MockHandler h;
  WuffsJsonCursor cursor(h);
  // 7 bytes fed, but the trailing '1' is an incomplete NUMBER: Wuffs rewinds
  // it into pending_bytes_, so only the 6 bytes before it are counted.
  ASSERT_TRUE(cursor.feed("{\"n\": 1", /*closed=*/false).ok());
  EXPECT_EQ(cursor.nextSourcePosition(), 6u);
  // Completing the number counts the held byte plus the new chunk.
  ASSERT_TRUE(cursor.feed("2}", /*closed=*/true).ok());
  EXPECT_EQ(cursor.nextSourcePosition(), 9u);
}

// Executable form of the documented enforcement loop: the outer filter checks
// nextSourcePosition() + chunk.size() against ParserConfig::max_body_bytes
// before each feed() and rejects instead of feeding. An oversized body must be
// rejected before its offending chunk is parsed.
TEST(MaxBodyBytesContractTest, PreFeedCheckRejectsBodyOverLimit) {
  ParserConfig cfg;
  cfg.max_body_bytes = 12;

  MockHandler h;
  WuffsJsonCursor cursor(h);
  const std::vector<absl::string_view> chunks = {R"({"a":true,)", R"("b":null})"}; // 10 + 9 bytes

  bool rejected = false;
  for (size_t i = 0; i < chunks.size(); ++i) {
    if (cursor.nextSourcePosition() + chunks[i].size() > cfg.max_body_bytes) {
      rejected = true; // filter would return ResourceExhausted here, chunk unparsed
      break;
    }
    ASSERT_TRUE(cursor.feed(chunks[i], /*closed=*/i + 1 == chunks.size()).ok());
  }

  EXPECT_TRUE(rejected);
  // Only the first chunk was fed: position shows the second never reached the cursor.
  EXPECT_EQ(cursor.nextSourcePosition(), chunks[0].size());
}

// ============================================================================
// Structural spec matching (matchesPatternPath) on real documents
// ============================================================================

// SpecMatchingHandler is the executable template for the production routing
// decision: convert spec.segments to PatternSegments once at config time,
// then ask the cursor for a structural match at each openStringCapture —
// zero allocations, and collision-free (see the hostile-key test below).
// canonicalPath() is a diagnostics-only serialization.

// Captures exactly the string values whose root-to-here chain structurally
// matches the spec's segments; each completed capture is terminated with ';'.
class SpecMatchingHandler : public MockHandler {
public:
  explicit SpecMatchingHandler(const ExtractFieldSpec& spec) {
    // Config-time conversion: string_views into the spec's stable segment
    // keys, so the per-callback match allocates nothing. `spec` must outlive
    // this handler.
    for (const auto& seg : spec.segments) {
      pattern_.push_back({seg.key, seg.is_array_element});
    }
  }

  void setCursor(const WuffsJsonCursor* cursor) { cursor_ = cursor; }

  bool openStringCapture(absl::string_view, int depth, size_t) override {
    capturing_ = cursor_->matchesPatternPath(pattern_, depth);
    return capturing_;
  }
  bool onStringChunk(absl::string_view, int, absl::string_view chunk) override {
    captured_.append(chunk.data(), chunk.size());
    return true;
  }
  void closeStringCapture(absl::string_view, int, size_t) override {
    if (capturing_) {
      captured_ += ';';
      capturing_ = false;
    }
  }

  const std::string& captured() const { return captured_; }

private:
  std::vector<WuffsJsonCursor::PatternSegment> pattern_;
  const WuffsJsonCursor* cursor_{nullptr};
  bool capturing_{false};
  std::string captured_;
};

TEST(StructuralMatchTest, RoutesArrayElementField) {
  auto spec = parseExtractFieldSpec("messages[].role");
  ASSERT_TRUE(spec.ok());
  SpecMatchingHandler h(*spec);
  WuffsJsonCursor cursor(h, /*track_paths=*/true);
  h.setCursor(&cursor);
  constexpr absl::string_view json =
      R"({"model":"m","messages":[{"role":"user","content":"h"},{"role":"tool","content":"x"}]})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  // Both role values captured; model and content are skipped despite also
  // being string values.
  EXPECT_EQ(h.captured(), "user;tool;");
}

TEST(StructuralMatchTest, RoutesDepthOneScalar) {
  auto spec = parseExtractFieldSpec("model");
  ASSERT_TRUE(spec.ok());
  SpecMatchingHandler h(*spec);
  WuffsJsonCursor cursor(h, /*track_paths=*/true);
  h.setCursor(&cursor);
  constexpr absl::string_view json = R"({"model":"m","messages":[{"role":"user"}]})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  EXPECT_EQ(h.captured(), "m;");
}

// Dict-only chain: every intermediate label comes from the push-key of the
// child container, not the current key.
TEST(StructuralMatchTest, RoutesNestedDicts) {
  auto spec = parseExtractFieldSpec("params._meta.traceparent");
  ASSERT_TRUE(spec.ok());
  SpecMatchingHandler h(*spec);
  WuffsJsonCursor cursor(h, /*track_paths=*/true);
  h.setCursor(&cursor);
  constexpr absl::string_view json =
      R"({"params":{"name":"n","_meta":{"traceparent":"t","other":"o"}}})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  // "name" (depth 2) and "other" (sibling at depth 3) are skipped.
  EXPECT_EQ(h.captured(), "t;");
}

// Nested arrays: the '[]' wildcard erases indices, so one spec matches the
// target field in every element of both array levels.
TEST(StructuralMatchTest, RoutesNestedArrays) {
  auto spec = parseExtractFieldSpec("messages[].content[].text");
  ASSERT_TRUE(spec.ok());
  SpecMatchingHandler h(*spec);
  WuffsJsonCursor cursor(h, /*track_paths=*/true);
  h.setCursor(&cursor);
  constexpr absl::string_view json = R"({"messages":[)"
                                     R"({"content":[{"text":"a"},{"text":"b"}]},)"
                                     R"({"content":[{"type":"x","text":"c"}]})"
                                     R"(]})";
  ASSERT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  // All three text values across both messages[] elements and all content[]
  // elements are captured; "type" at the same depth is skipped.
  EXPECT_EQ(h.captured(), "a;b;c;");
}

// For collision case that {"":{"a.b":"decoy"}} produced the same (string "a.b", depth 2) pair as
// the legitimate {"a":{"b":...}}. matchesPatternPath() compares labels per level with no
// serialization: the decoy's "" label can never equal segment "a", and its "a.b" label can never
// span the two segments a,b.
TEST(StructuralMatchTest, RejectsHostileKeyCollision) {
  auto spec = parseExtractFieldSpec("a.b");
  ASSERT_TRUE(spec.ok());
  ASSERT_EQ(spec->depth(), 2);

  {
    SpecMatchingHandler h(*spec);
    WuffsJsonCursor cursor(h, /*track_paths=*/true);
    h.setCursor(&cursor);
    // The shape the spec means: dict "a" containing key "b".
    ASSERT_TRUE(cursor.feed(R"({"a":{"b":"legit"}})", /*closed=*/true).ok());
    EXPECT_EQ(h.captured(), "legit;");
  }
  {
    SpecMatchingHandler h(*spec);
    WuffsJsonCursor cursor(h, /*track_paths=*/true);
    h.setCursor(&cursor);
    // Hostile shape that collides under string equality (same serialized
    // string, same depth): structural matching rejects it at level 1.
    ASSERT_TRUE(cursor.feed(R"({"":{"a.b":"decoy"}})", /*closed=*/true).ok());
    EXPECT_EQ(h.captured(), ""); // decoy not captured; hole closed
  }
}

// ============================================================================
// parseExtractFieldSpec — valid paths
// ============================================================================

TEST(ParseExtractFieldSpecTest, DepthOneScalar) {
  auto result = parseExtractFieldSpec("model");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 1u);
  EXPECT_EQ(result->segments[0].key, "model");
  EXPECT_FALSE(result->segments[0].is_array_element);
  EXPECT_EQ(result->depth(), 1);
  EXPECT_EQ(result->path, "model");
}

TEST(ParseExtractFieldSpecTest, DepthOneArray) {
  auto result = parseExtractFieldSpec("messages[]");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 2u);
  EXPECT_EQ(result->segments[0].key, "messages");
  EXPECT_FALSE(result->segments[0].is_array_element);
  EXPECT_TRUE(result->segments[1].is_array_element);
  EXPECT_EQ(result->depth(), 2);
}

TEST(ParseExtractFieldSpecTest, DepthTwoScalarInArray) {
  auto result = parseExtractFieldSpec("messages[].role");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 3u);
  EXPECT_EQ(result->segments[0].key, "messages");
  EXPECT_TRUE(result->segments[1].is_array_element);
  EXPECT_EQ(result->segments[2].key, "role");
  EXPECT_EQ(result->depth(), 3);
}

TEST(ParseExtractFieldSpecTest, DepthThreeNestedDicts) {
  auto result = parseExtractFieldSpec("params._meta.traceparent");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 3u);
  EXPECT_EQ(result->segments[0].key, "params");
  EXPECT_EQ(result->segments[1].key, "_meta");
  EXPECT_EQ(result->segments[2].key, "traceparent");
  EXPECT_EQ(result->depth(), 3);
}

TEST(ParseExtractFieldSpecTest, NestedArrays) {
  auto result = parseExtractFieldSpec("messages[].content[]");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 4u);
  EXPECT_EQ(result->segments[0].key, "messages");
  EXPECT_TRUE(result->segments[1].is_array_element);
  EXPECT_EQ(result->segments[2].key, "content");
  EXPECT_TRUE(result->segments[3].is_array_element);
  EXPECT_EQ(result->depth(), 4);
}

TEST(ParseExtractFieldSpecTest, RootArray) {
  // "[].role" is syntactically valid even if unusual in practice.
  auto result = parseExtractFieldSpec("[].role");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 2u);
  EXPECT_TRUE(result->segments[0].is_array_element);
  EXPECT_EQ(result->segments[1].key, "role");
  EXPECT_EQ(result->depth(), 2);
}

TEST(ParseExtractFieldSpecTest, UnderscorePrefixedKey) {
  auto result = parseExtractFieldSpec("_meta");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->segments.size(), 1u);
  EXPECT_EQ(result->segments[0].key, "_meta");
}

// ============================================================================
// parseExtractFieldSpec — malformed paths
// ============================================================================

TEST(ParseExtractFieldSpecTest, EmptyPathRejected) { EXPECT_FALSE(parseExtractFieldSpec("").ok()); }

TEST(ParseExtractFieldSpecTest, LeadingDotRejected) {
  EXPECT_FALSE(parseExtractFieldSpec(".model").ok());
}

TEST(ParseExtractFieldSpecTest, TrailingDotRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("model.").ok());
}

TEST(ParseExtractFieldSpecTest, DoubleDotRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("params..name").ok());
}

TEST(ParseExtractFieldSpecTest, DotBeforeArrayRejected) {
  // The canonical pattern-path syntax never contains '.[]' — '[]' always directly follows
  // the parent key.
  EXPECT_FALSE(parseExtractFieldSpec("messages.[]").ok());
}

TEST(ParseExtractFieldSpecTest, UnmatchedOpenBracketRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("messages[").ok());
}

TEST(ParseExtractFieldSpecTest, UnmatchedCloseBracketRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("messages]").ok());
}

TEST(ParseExtractFieldSpecTest, NonEmptySubscriptRejected) {
  // Only '[]' is valid — '[0]', '[key]', '[*]' are not supported.
  EXPECT_FALSE(parseExtractFieldSpec("messages[0]").ok());
  EXPECT_FALSE(parseExtractFieldSpec("messages[key]").ok());
}

TEST(ParseExtractFieldSpecTest, NestedBracketsRejected) {
  EXPECT_FALSE(parseExtractFieldSpec("a[[]]").ok());
}

TEST(ParseExtractFieldSpecTest, KeyAfterBracketWithoutDotRejected) {
  // The canonical pattern-path syntax always separates an array wildcard from
  // a following dict key with '.' ("a[].b", never "a[]b") — a bare key after
  // ']' is a typo.
  EXPECT_FALSE(parseExtractFieldSpec("a[]b").ok());
  EXPECT_FALSE(parseExtractFieldSpec("messages[]role").ok());
}

TEST(ParseExtractFieldSpecTest, BracketAfterBracketAccepted) {
  // '[' directly after ']' stays legal: "a[][]" is a dict key whose value is
  // an array of arrays (depth 3: key, outer wildcard, inner wildcard).
  auto result = parseExtractFieldSpec("a[][]");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->depth(), 3);
  EXPECT_EQ(result->canonicalPath(), "a[][]");
}

TEST(ParseExtractFieldSpecTest, DepthBoundEnforced) {
  constexpr absl::string_view kNineSegments = "a.b.c.d.e.f.g.h.i";
  // Within bound and at the exact bound: accepted.
  EXPECT_TRUE(parseExtractFieldSpec(kNineSegments, 9).ok());
  // Over bound: rejected with an informative error.
  auto result = parseExtractFieldSpec(kNineSegments, 8);
  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.status().message().empty());
  // Default (0) means no depth check.
  EXPECT_TRUE(parseExtractFieldSpec(kNineSegments).ok());
}

// Error messages must be non-empty and contain relevant context (not just "false").
TEST(ParseExtractFieldSpecTest, ErrorMessageIsInformative) {
  auto result = parseExtractFieldSpec("messages.[]");
  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.status().message().empty());
}

// ============================================================================
// ExtractFieldSpec::canonicalPath
// ============================================================================

// canonicalPath() must reproduce the accepted input syntax exactly (RoundTrip
// below). It is diagnostics-only: routing compares segments structurally — see
// the StructuralMatchTest section further below.

// Canonical path is round-trippable: parse → canonicalPath == original path.
TEST(CanonicalPathTest, RoundTrip) {
  const std::vector<std::string> paths = {
      "model",
      "messages[]",
      "messages[].role",
      "params._meta.traceparent",
      "messages[].content[]",
      "_meta",
      "[].role",
      "a[][]",
      "[][]",
  };
  for (const auto& p : paths) {
    auto result = parseExtractFieldSpec(p);
    ASSERT_TRUE(result.ok()) << "failed to parse: " << p;
    EXPECT_EQ(result->canonicalPath(), p) << "round-trip failed for: " << p;
  }
}

// ============================================================================
// ParserConfig::validate — extraction mode mutual exclusion
// ============================================================================

TEST(ParserConfigValidateTest, DefaultConfigIsValid) {
  ParserConfig cfg;
  EXPECT_TRUE(cfg.validate().ok());
}

TEST(ParserConfigValidateTest, CaptureAllAloneIsValid) {
  ParserConfig cfg;
  cfg.capture_all_scalars = true;
  EXPECT_TRUE(cfg.validate().ok());
}

TEST(ParserConfigValidateTest, SpecsAloneAreValid) {
  ParserConfig cfg;
  auto spec = parseExtractFieldSpec("model");
  ASSERT_TRUE(spec.ok());
  cfg.extract_fields.push_back(std::move(*spec));
  EXPECT_TRUE(cfg.validate().ok());
}

TEST(ParserConfigValidateTest, CaptureAllWithSpecsRejected) {
  ParserConfig cfg;
  cfg.capture_all_scalars = true;
  auto spec = parseExtractFieldSpec("model");
  ASSERT_TRUE(spec.ok());
  cfg.extract_fields.push_back(std::move(*spec));
  auto status = cfg.validate();
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_FALSE(status.message().empty());
}

} // namespace
} // namespace Wuffs
} // namespace Json
} // namespace Envoy
