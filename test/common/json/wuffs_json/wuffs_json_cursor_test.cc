#include <string>
#include <vector>

#include "source/common/json/wuffs_json/wuffs_json_cursor.h"

#include "absl/strings/numbers.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

// ── Capturing handler ─────────────────────────────────────────────────────────
// Records depth-1 fields from a JSON object. Deeper content is discarded.
// Scalars are stored as raw strings; the test asserts on them directly.

struct CapturingHandler : WuffsJsonCursor::Handler {
  struct Field {
    std::string key;
    std::string str_val; // for string fields
    std::string raw_val; // for scalars (number / bool / null as string)
    bool is_string{false};
    bool is_scalar{false};
  };

  std::vector<Field> fields;
  std::string pending_key_;
  std::string pending_str_;

  std::string* openStringCapture(absl::string_view /*key*/, int depth,
                                 size_t /*token_start*/) override {
    if (depth == 1) {
      pending_str_.clear();
      return &pending_str_;
    }
    return nullptr;
  }

  void closeStringCapture(std::string* target, absl::string_view /*key*/, int /*depth*/,
                          size_t /*token_end*/) override {
    fields.push_back({pending_key_, *target, {}, /*is_string=*/true});
  }

  absl::Status onKey(absl::string_view key, int depth, size_t /*token_start*/) override {
    if (depth == 1)
      pending_key_ = std::string(key);
    return absl::OkStatus();
  }

  absl::Status onNumber(absl::string_view /*key*/, absl::string_view raw, int depth,
                        size_t /*token_start*/, size_t /*token_end*/) override {
    if (depth == 1)
      fields.push_back({pending_key_, {}, std::string(raw), {}, /*is_scalar=*/true});
    return absl::OkStatus();
  }

  absl::Status onBoolean(absl::string_view /*key*/, bool value, int depth, size_t /*token_start*/,
                         size_t /*token_end*/) override {
    if (depth == 1)
      fields.push_back({pending_key_, {}, value ? "true" : "false", {}, /*is_scalar=*/true});
    return absl::OkStatus();
  }

  void onNull(absl::string_view /*key*/, int depth, size_t /*token_start*/,
              size_t /*token_end*/) override {
    if (depth == 1)
      fields.push_back({pending_key_, {}, "null", {}, /*is_scalar=*/true});
  }

  void onContainerOpen(absl::string_view /*key*/, bool /*is_dict*/, int /*depth*/,
                       size_t /*token_start*/) override {}
  void onContainerClose(int /*depth*/, size_t /*token_end*/) override {}
};

// Helper: parse a complete JSON string in one shot.
absl::Status parse(absl::string_view json, CapturingHandler& h) {
  WuffsJsonCursor cursor(h);
  return cursor.feed(json, /*closed=*/true);
}

// ── Abort handler ─────────────────────────────────────────────────────────────
// Returns non-OK from onNumber to verify feed() propagates handler errors.

struct AbortOnNumberHandler : CapturingHandler {
  absl::Status onNumber(absl::string_view, absl::string_view, int, size_t, size_t) override {
    return absl::InternalError("handler abort");
  }
};

// ── Byte-range handler ────────────────────────────────────────────────────────
// Records the first container open/close offsets and the first key/value offsets
// to verify token_start / token_end byte positions are correct.

struct ByteRangeHandler : WuffsJsonCursor::Handler {
  static constexpr size_t kNotSet = size_t(-1);
  size_t container_open_start{kNotSet};
  size_t container_close_end{kNotSet};
  size_t first_key_start{kNotSet};
  size_t first_value_end{kNotSet};

  std::string* openStringCapture(absl::string_view, int, size_t) override { return nullptr; }
  void closeStringCapture(std::string*, absl::string_view, int, size_t token_end) override {
    if (first_value_end == kNotSet)
      first_value_end = token_end;
  }
  absl::Status onKey(absl::string_view, int, size_t token_start) override {
    if (first_key_start == kNotSet)
      first_key_start = token_start;
    return absl::OkStatus();
  }
  absl::Status onNumber(absl::string_view, absl::string_view, int, size_t,
                        size_t token_end) override {
    if (first_value_end == kNotSet)
      first_value_end = token_end;
    return absl::OkStatus();
  }
  absl::Status onBoolean(absl::string_view, bool, int, size_t, size_t token_end) override {
    if (first_value_end == kNotSet)
      first_value_end = token_end;
    return absl::OkStatus();
  }
  void onNull(absl::string_view, int, size_t, size_t token_end) override {
    if (first_value_end == kNotSet)
      first_value_end = token_end;
  }
  void onContainerOpen(absl::string_view, bool, int, size_t token_start) override {
    if (container_open_start == kNotSet)
      container_open_start = token_start;
  }
  void onContainerClose(int, size_t token_end) override { container_close_end = token_end; }
};

// ── Path-tracking handler ─────────────────────────────────────────────────────
// Records buildIndexedPath() at each leaf callback (scalar or string value).
// Captures all string values at every depth so path tests aren't depth-gated.

struct PathCapturingHandler : WuffsJsonCursor::Handler {
  WuffsJsonCursor* cursor = nullptr;
  std::vector<std::string> paths;
  std::string str_buf_;

  std::string* openStringCapture(absl::string_view, int, size_t) override {
    str_buf_.clear();
    return &str_buf_;
  }
  void closeStringCapture(std::string*, absl::string_view, int depth, size_t) override {
    if (cursor)
      paths.push_back(cursor->buildIndexedPath(depth));
  }
  absl::Status onKey(absl::string_view, int, size_t) override { return absl::OkStatus(); }
  absl::Status onNumber(absl::string_view, absl::string_view, int depth, size_t, size_t) override {
    if (cursor)
      paths.push_back(cursor->buildIndexedPath(depth));
    return absl::OkStatus();
  }
  absl::Status onBoolean(absl::string_view, bool, int depth, size_t, size_t) override {
    if (cursor)
      paths.push_back(cursor->buildIndexedPath(depth));
    return absl::OkStatus();
  }
  void onNull(absl::string_view, int depth, size_t, size_t) override {
    if (cursor)
      paths.push_back(cursor->buildIndexedPath(depth));
  }
  void onContainerOpen(absl::string_view, bool, int, size_t) override {}
  void onContainerClose(int, size_t) override {}
};

absl::Status parsePaths(absl::string_view json, PathCapturingHandler& h, bool pattern = false) {
  WuffsJsonCursor cursor(h, /*track_paths=*/true);
  h.cursor = &cursor;
  (void)pattern; // buildPatternPath tests call cursor directly from the handler
  return cursor.feed(json, /*closed=*/true);
}

// Same handler but records buildPatternPath instead of buildIndexedPath.
struct PatternCapturingHandler : PathCapturingHandler {
  void closeStringCapture(std::string*, absl::string_view, int depth, size_t) override {
    if (cursor)
      paths.push_back(cursor->buildPatternPath(depth));
  }
  absl::Status onNumber(absl::string_view, absl::string_view, int depth, size_t, size_t) override {
    if (cursor)
      paths.push_back(cursor->buildPatternPath(depth));
    return absl::OkStatus();
  }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(WuffsJsonCursorTest, EmptyObject) {
  CapturingHandler h;
  EXPECT_TRUE(parse("{}", h).ok());
  EXPECT_TRUE(h.fields.empty());
}

TEST(WuffsJsonCursorTest, FlatStringFields) {
  CapturingHandler h;
  EXPECT_TRUE(parse(R"({"model":"gpt-4","role":"user"})", h).ok());
  ASSERT_EQ(h.fields.size(), 2u);
  EXPECT_EQ(h.fields[0].key, "model");
  EXPECT_EQ(h.fields[0].str_val, "gpt-4");
  EXPECT_EQ(h.fields[1].key, "role");
  EXPECT_EQ(h.fields[1].str_val, "user");
}

TEST(WuffsJsonCursorTest, ScalarFields) {
  CapturingHandler h;
  EXPECT_TRUE(parse(R"({"count":42,"ratio":1.5,"ok":true,"x":null})", h).ok());
  ASSERT_EQ(h.fields.size(), 4u);
  EXPECT_EQ(h.fields[0].raw_val, "42");
  EXPECT_EQ(h.fields[1].raw_val, "1.5");
  EXPECT_EQ(h.fields[2].raw_val, "true");
  EXPECT_EQ(h.fields[3].raw_val, "null");
}

// Wuffs emits \n, \t, and \uXXXX as UNICODE_CODE_POINT tokens (VBC=3),
// not STRING tokens. This test verifies the cursor handles them correctly.
TEST(WuffsJsonCursorTest, StringEscapes) {
  CapturingHandler h;
  EXPECT_TRUE(parse(R"({"nl":"hello\nworld","tab":"a\tb","uni":"A"})", h).ok());
  ASSERT_EQ(h.fields.size(), 3u);
  EXPECT_EQ(h.fields[0].str_val, "hello\nworld");
  EXPECT_EQ(h.fields[1].str_val, "a\tb");
  EXPECT_EQ(h.fields[2].str_val, "A"); // U+0041
}

// \uXXXX escapes for code points above U+007F arrive as UNICODE_CODE_POINT tokens
// and must be encoded to multi-byte UTF-8 by appendCodePoint.
// \u00C9 (U+00C9, É) → 2 UTF-8 bytes C3 89.
// \u4E2D (U+4E2D, 中) → 3 UTF-8 bytes E4 B8 AD.
TEST(WuffsJsonCursorTest, UnicodeEscapeMultiByteUtf8) {
  CapturingHandler h;
  EXPECT_TRUE(parse(R"({"a":"\u00C9","b":"\u4E2D"})", h).ok());
  ASSERT_EQ(h.fields.size(), 2u);
  EXPECT_EQ(h.fields[0].str_val, "\xC3\x89");     // É
  EXPECT_EQ(h.fields[1].str_val, "\xE4\xB8\xAD"); // 中
}

// Supplementary characters (> U+FFFF) are written in JSON as surrogate pairs
// (\uHHHH\uLLLL). Wuffs combines them into a single UNICODE_CODE_POINT token
// with the full code point value; appendCodePoint encodes it as 4-byte UTF-8.
// \uD83D\uDE00 → U+1F600 (😀) → F0 9F 98 80.
TEST(WuffsJsonCursorTest, UnicodeSurrogatePairDecodedToUtf8) {
  CapturingHandler h;
  EXPECT_TRUE(parse(R"({"a":"\uD83D\uDE00"})", h).ok());
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].str_val, "\xF0\x9F\x98\x80"); // 😀
}

// Deeper-than-1 content is discarded (openStringCapture returns nullptr).
TEST(WuffsJsonCursorTest, NestedObjectDiscarded) {
  CapturingHandler h;
  EXPECT_TRUE(parse(R"({"top":"v","nested":{"a":"b"}})", h).ok());
  // "top" is a depth-1 string → captured.
  // Inner "a"/"b" at depth 2 have openStringCapture return nullptr → discarded.
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].str_val, "v");
}

// Feed the document in two chunks to verify the Wuffs decoder state persists.
TEST(WuffsJsonCursorTest, StreamingAcrossChunks) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);

  // The string value "gpt-4" straddles the chunk boundary.
  EXPECT_TRUE(cursor.feed(R"({"model":"gpt)", /*closed=*/false).ok());
  EXPECT_TRUE(cursor.feed(R"(-4","n":7})", /*closed=*/true).ok());

  ASSERT_EQ(h.fields.size(), 2u);
  EXPECT_EQ(h.fields[0].str_val, "gpt-4");
  EXPECT_EQ(h.fields[1].raw_val, "7");
}

TEST(WuffsJsonCursorTest, InvalidJsonReturnsError) {
  CapturingHandler h;
  EXPECT_FALSE(parse("not json", h).ok());
}

TEST(WuffsJsonCursorTest, DuplicateKeyRejected) {
  CapturingHandler h;
  EXPECT_FALSE(parse(R"({"model":"gpt-4","model":"gpt-4o"})", h).ok());
}

TEST(WuffsJsonCursorTest, DuplicateKeyInNestedObjectRejected) {
  CapturingHandler h;
  EXPECT_FALSE(parse(R"({"x":{"a":1,"a":2}})", h).ok());
}

// Same key name at different nesting depths must not trigger a false positive.
TEST(WuffsJsonCursorTest, SameKeyNameAtDifferentDepthsAllowed) {
  CapturingHandler h;
  EXPECT_TRUE(parse(R"({"a":{"a":1}})", h).ok());
}

// Same key name in sibling objects must not trigger a false positive.
TEST(WuffsJsonCursorTest, SameKeyNameInSiblingObjectsAllowed) {
  CapturingHandler h;
  EXPECT_TRUE(parse(R"([{"a":1},{"a":2}])", h).ok());
}

// ── Key-length enforcement tests ──────────────────────────────────────────────

// Key exactly at kMaxKeyBytes (256) must be accepted.
TEST(WuffsJsonCursorTest, KeyAtMaxBytesAccepted) {
  CapturingHandler h;
  const std::string key(256, 'k');
  EXPECT_TRUE(parse("{\"" + key + "\":1}", h).ok());
}

// Key one byte over kMaxKeyBytes must be rejected mid-accumulation.
TEST(WuffsJsonCursorTest, KeyExceedsMaxBytesRejected) {
  CapturingHandler h;
  const std::string key(257, 'k');
  EXPECT_FALSE(parse("{\"" + key + "\":1}", h).ok());
}

// Same boundary delivered across two chunks: chunk1 ends inside the key (no
// closing quote yet); chunk2 delivers only the closing quote as a DROP STRING
// token. The pre-check must skip DROP tokens — otherwise the closing quote's
// token_len=1 would push 256+1 over the limit and wrongly reject a valid key.
TEST(WuffsJsonCursorTest, KeyAtMaxBytesAcceptedSplitAcrossChunks) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_TRUE(cursor.feed("{\"" + std::string(256, 'k'), /*closed=*/false).ok());
  EXPECT_TRUE(cursor.feed("\":1}", /*closed=*/true).ok());
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].key.size(), 256u);
}

// Key whose last byte comes from a \n escape (UNICODE_CODE_POINT token).
// \n is 2 raw source bytes but decodes to 1 UTF-8 byte; the pre-check must
// use the decoded size (1), not token_len (2), or 255+2 would wrongly reject.
TEST(WuffsJsonCursorTest, KeyAtMaxBytesWithEscapeAccepted) {
  CapturingHandler h;
  // 255 plain bytes + \n (1 decoded byte) = 256 bytes total.
  EXPECT_TRUE(parse("{\"" + std::string(255, 'k') + R"(\n":1})", h).ok());
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].key.size(), 256u);
}

// 256 plain bytes + \n (1 decoded byte) = 257 bytes — must be rejected.
TEST(WuffsJsonCursorTest, KeyExceedsMaxBytesWithEscapeRejected) {
  CapturingHandler h;
  EXPECT_FALSE(parse("{\"" + std::string(256, 'k') + R"(\n":1})", h).ok());
}

// ── Handler abort propagation ─────────────────────────────────────────────────

// A non-OK status returned from onNumber must cause feed() to return that error.
TEST(WuffsJsonCursorTest, HandlerAbortPropagated) {
  AbortOnNumberHandler h;
  EXPECT_FALSE(parse(R"({"n":1})", h).ok());
}

// ── Byte-range offset tests ───────────────────────────────────────────────────

// The root container's [token_start, token_end) byte range must cover the full input.
TEST(WuffsJsonCursorTest, ByteRangeContainerCoversWholeInput) {
  constexpr absl::string_view json = R"({"a":1})";
  ByteRangeHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  EXPECT_EQ(h.container_open_start, 0u);
  EXPECT_EQ(h.container_close_end, json.size());
}

// [onKey.token_start, value.token_end) must cover the complete "key":value field
// verbatim, suitable for zero-copy passthrough.
TEST(WuffsJsonCursorTest, ByteRangeKeyValueField) {
  constexpr absl::string_view json = R"({"a":1})";
  ByteRangeHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_TRUE(cursor.feed(json, /*closed=*/true).ok());
  ASSERT_NE(h.first_key_start, ByteRangeHandler::kNotSet);
  ASSERT_NE(h.first_value_end, ByteRangeHandler::kNotSet);
  EXPECT_EQ(json.substr(h.first_key_start, h.first_value_end - h.first_key_start), R"("a":1)");
}

// ── Depth limit tests ─────────────────────────────────────────────────────────

// 8 levels of nesting must be accepted (boundary value for default max_depth=8).
TEST(WuffsJsonCursorTest, MaxDepthAccepted) {
  CapturingHandler h;
  // 8 nested dicts, scalar at the innermost level.
  EXPECT_TRUE(parse(R"({"a":{"a":{"a":{"a":{"a":{"a":{"a":{"a":1}}}}}}}}})", h).ok());
}

// 9 levels of nesting must be rejected by default (fail-closed).
TEST(WuffsJsonCursorTest, ExceedDefaultMaxDepthRejected) {
  CapturingHandler h;
  EXPECT_FALSE(parse(R"({"a":{"a":{"a":{"a":{"a":{"a":{"a":{"a":{"a":1}}}}}}}}}})", h).ok());
}

// Caller may raise the limit to accept deeper JSON.
TEST(WuffsJsonCursorTest, CustomMaxDepthAccepted) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h, /*track_paths=*/false, /*max_depth=*/12);
  EXPECT_TRUE(cursor
                  .feed(R"({"a":{"a":{"a":{"a":{"a":{"a":{"a":{"a":{"a":1}}}}}}}}}}})",
                        /*closed=*/true)
                  .ok());
}

// ── Path-tracking tests ───────────────────────────────────────────────────────

// Scalar elements in an array must each get a distinct 0-based index.
// Before the fix, array_index_ was only incremented on container close, so
// [1,2,3] would report [0] for every element.
TEST(WuffsJsonCursorTest, BuildIndexedPathScalarsInArray) {
  PathCapturingHandler h;
  EXPECT_TRUE(parsePaths("[1, 2, 3]", h).ok());
  ASSERT_EQ(h.paths.size(), 3u);
  EXPECT_EQ(h.paths[0], "[0]");
  EXPECT_EQ(h.paths[1], "[1]");
  EXPECT_EQ(h.paths[2], "[2]");
}

// String elements in an array must also advance the index counter.
TEST(WuffsJsonCursorTest, BuildIndexedPathStringsInArray) {
  PathCapturingHandler h;
  EXPECT_TRUE(parsePaths(R"(["a","b","c"])", h).ok());
  ASSERT_EQ(h.paths.size(), 3u);
  EXPECT_EQ(h.paths[0], "[0]");
  EXPECT_EQ(h.paths[1], "[1]");
  EXPECT_EQ(h.paths[2], "[2]");
}

// Mixed array: scalar, then nested object.
// The scalar must increment the index so the object is reported at [1].
TEST(WuffsJsonCursorTest, BuildIndexedPathMixedScalarThenObject) {
  PathCapturingHandler h;
  // "2" is a number inside {"a":2} at depth 2; path should be "[1].a".
  EXPECT_TRUE(parsePaths(R"([1, {"a": 2}])", h).ok());
  ASSERT_EQ(h.paths.size(), 2u);
  EXPECT_EQ(h.paths[0], "[0]");   // scalar 1 at depth 1
  EXPECT_EQ(h.paths[1], "[1].a"); // number 2 at depth 2, inside second element
}

// Typical LLM request shape: messages array of role/content objects.
// Each object's string value must be reported under the correct array index.
TEST(WuffsJsonCursorTest, BuildIndexedPathNestedMessages) {
  PathCapturingHandler h;
  EXPECT_TRUE(parsePaths(R"({"messages":[{"role":"user"},{"role":"assistant"}]})", h).ok());
  ASSERT_EQ(h.paths.size(), 2u);
  EXPECT_EQ(h.paths[0], "messages[0].role");
  EXPECT_EQ(h.paths[1], "messages[1].role");
}

// buildPatternPath must use [] instead of [n] and be identical for each element.
TEST(WuffsJsonCursorTest, BuildPatternPathNestedMessages) {
  PatternCapturingHandler h;
  WuffsJsonCursor cursor(h, /*track_paths=*/true);
  h.cursor = &cursor;
  EXPECT_TRUE(cursor
                  .feed(R"({"messages":[{"role":"user"},{"role":"assistant"}]})",
                        /*closed=*/true)
                  .ok());
  ASSERT_EQ(h.paths.size(), 2u);
  EXPECT_EQ(h.paths[0], "messages[].role");
  EXPECT_EQ(h.paths[1], "messages[].role");
}

} // namespace
} // namespace Json
} // namespace Envoy
