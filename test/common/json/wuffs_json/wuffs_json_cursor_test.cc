#include <string>
#include <vector>

#include "source/common/json/wuffs_json/wuffs_json_cursor.h"

#include "test/test_common/status_utility.h"

#include "absl/strings/numbers.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace Wuffs {
namespace {

using ::Envoy::StatusHelpers::IsOk;
using ::testing::Not;

// Capturing handler
// Records depth-1 fields from a JSON object. Deeper content is discarded.
// Scalars are stored as raw strings; the test asserts on them directly.

class CapturingHandler : public WuffsJsonCursor::Handler {
public:
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
  bool capturing_{false};

  bool openStringCapture(absl::string_view /*key*/, int depth, size_t /*token_start*/) override {
    capturing_ = (depth == 1);
    if (capturing_) {
      pending_str_.clear();
    }
    return capturing_;
  }

  bool onStringChunk(absl::string_view /*key*/, int /*depth*/, absl::string_view chunk) override {
    pending_str_.append(chunk);
    return true;
  }

  void closeStringCapture(absl::string_view /*key*/, int /*depth*/, size_t /*token_end*/) override {
    if (capturing_) {
      fields.push_back({pending_key_, pending_str_, {}, /*is_string=*/true});
    }
  }

  absl::Status onKey(absl::string_view key, int depth, size_t /*token_start*/) override {
    if (depth == 1) {
      pending_key_ = std::string(key);
    }
    return absl::OkStatus();
  }

  absl::Status onNumber(absl::string_view /*key*/, absl::string_view raw, int depth,
                        size_t /*token_start*/, size_t /*token_end*/) override {
    if (depth == 1) {
      fields.push_back({pending_key_, {}, std::string(raw), {}, /*is_scalar=*/true});
    }
    return absl::OkStatus();
  }

  absl::Status onBoolean(absl::string_view /*key*/, bool value, int depth, size_t /*token_start*/,
                         size_t /*token_end*/) override {
    if (depth == 1) {
      fields.push_back({pending_key_, {}, value ? "true" : "false", {}, /*is_scalar=*/true});
    }
    return absl::OkStatus();
  }

  void onNull(absl::string_view /*key*/, int depth, size_t /*token_start*/,
              size_t /*token_end*/) override {
    if (depth == 1) {
      fields.push_back({pending_key_, {}, "null", {}, /*is_scalar=*/true});
    }
  }

  void onContainerOpen(absl::string_view /*key*/, bool /*is_dict*/, int /*depth*/,
                       size_t /*token_start*/) override {}
  void onContainerClose(int /*depth*/, size_t /*token_end*/) override {}
};

// Helper: parse a complete JSON string in one shot.
absl::Status parse(absl::string_view json, WuffsJsonCursor::Handler& h) {
  WuffsJsonCursor cursor(h);
  return cursor.feed(json, /*closed=*/true);
}

// Abort handler
// Returns non-OK from onNumber to verify feed() propagates handler errors.

class AbortOnNumberHandler : public CapturingHandler {
public:
  absl::Status onNumber(absl::string_view, absl::string_view, int, size_t, size_t) override {
    return absl::InternalError("handler abort");
  }
};

// Byte-range handler
// Records the first container open/close offsets and the first key/value offsets
// to verify token_start / token_end byte positions are correct.
class ByteRangeHandler : public WuffsJsonCursor::Handler {
public:
  static constexpr size_t kNotSet = size_t(-1);
  size_t container_open_start{kNotSet};
  size_t container_close_end{kNotSet};
  size_t first_key_start{kNotSet};
  size_t first_value_end{kNotSet};

  bool openStringCapture(absl::string_view, int, size_t) override { return false; }
  bool onStringChunk(absl::string_view, int, absl::string_view) override { return true; }
  void closeStringCapture(absl::string_view, int, size_t token_end) override {
    if (first_value_end == kNotSet) {
      first_value_end = token_end;
    }
  }
  absl::Status onKey(absl::string_view, int, size_t token_start) override {
    if (first_key_start == kNotSet) {
      first_key_start = token_start;
    }
    return absl::OkStatus();
  }
  absl::Status onNumber(absl::string_view, absl::string_view, int, size_t,
                        size_t token_end) override {
    if (first_value_end == kNotSet) {
      first_value_end = token_end;
    }
    return absl::OkStatus();
  }
  absl::Status onBoolean(absl::string_view, bool, int, size_t, size_t token_end) override {
    if (first_value_end == kNotSet) {
      first_value_end = token_end;
    }
    return absl::OkStatus();
  }
  void onNull(absl::string_view, int, size_t, size_t token_end) override {
    if (first_value_end == kNotSet) {
      first_value_end = token_end;
    }
  }
  void onContainerOpen(absl::string_view, bool, int, size_t token_start) override {
    if (container_open_start == kNotSet) {
      container_open_start = token_start;
    }
  }
  void onContainerClose(int, size_t token_end) override { container_close_end = token_end; }
};

// Path-tracking handler
// Records buildIndexedPath() at each leaf callback (scalar or string value).
// Captures all string values at every depth so path tests aren't depth-gated.

class PathCapturingHandler : public WuffsJsonCursor::Handler {
public:
  WuffsJsonCursor* cursor = nullptr;
  std::vector<std::string> paths;
  std::string str_buf_;

  bool openStringCapture(absl::string_view, int, size_t) override {
    str_buf_.clear();
    return true;
  }
  bool onStringChunk(absl::string_view, int, absl::string_view chunk) override {
    str_buf_.append(chunk);
    return true;
  }
  void closeStringCapture(absl::string_view, int depth, size_t) override {
    if (cursor) {
      paths.push_back(cursor->buildIndexedPath(depth));
    }
  }
  absl::Status onKey(absl::string_view, int, size_t) override { return absl::OkStatus(); }
  absl::Status onNumber(absl::string_view, absl::string_view, int depth, size_t, size_t) override {
    if (cursor) {
      paths.push_back(cursor->buildIndexedPath(depth));
    }
    return absl::OkStatus();
  }
  absl::Status onBoolean(absl::string_view, bool, int depth, size_t, size_t) override {
    if (cursor) {
      paths.push_back(cursor->buildIndexedPath(depth));
    }
    return absl::OkStatus();
  }
  void onNull(absl::string_view, int depth, size_t, size_t) override {
    if (cursor) {
      paths.push_back(cursor->buildIndexedPath(depth));
    }
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
class PatternCapturingHandler : public PathCapturingHandler {
public:
  void closeStringCapture(absl::string_view, int depth, size_t) override {
    if (cursor) {
      paths.push_back(cursor->buildPatternPath(depth));
    }
  }
  absl::Status onNumber(absl::string_view, absl::string_view, int depth, size_t, size_t) override {
    if (cursor) {
      paths.push_back(cursor->buildPatternPath(depth));
    }
    return absl::OkStatus();
  }
};

// Tests

TEST(WuffsJsonCursorTest, EmptyObject) {
  CapturingHandler h;
  EXPECT_OK(parse("{}", h));
  EXPECT_TRUE(h.fields.empty());
}

TEST(WuffsJsonCursorTest, FlatStringFields) {
  CapturingHandler h;
  EXPECT_OK(parse(R"({"model":"gpt-4","role":"user"})", h));
  ASSERT_EQ(h.fields.size(), 2u);
  EXPECT_EQ(h.fields[0].key, "model");
  EXPECT_EQ(h.fields[0].str_val, "gpt-4");
  EXPECT_EQ(h.fields[1].key, "role");
  EXPECT_EQ(h.fields[1].str_val, "user");
}

TEST(WuffsJsonCursorTest, ScalarFields) {
  CapturingHandler h;
  EXPECT_OK(parse(R"({"count":42,"ratio":1.5,"ok":true,"x":null})", h));
  ASSERT_EQ(h.fields.size(), 4u);
  EXPECT_EQ(h.fields[0].raw_val, "42");
  EXPECT_EQ(h.fields[1].raw_val, "1.5");
  EXPECT_EQ(h.fields[2].raw_val, "true");
  EXPECT_EQ(h.fields[3].raw_val, "null");
}

// Wuffs emits \n, \t as UNICODE_CODE_POINT tokens (VBC=3),
// not STRING tokens. This test verifies the cursor handles them correctly.
TEST(WuffsJsonCursorTest, StringEscapes) {
  CapturingHandler h;
  EXPECT_OK(parse(R"({"nl":"hello\nworld","tab":"a\tb","uni":"A"})", h));
  ASSERT_EQ(h.fields.size(), 3u);
  EXPECT_EQ(h.fields[0].str_val, "hello\nworld");
  EXPECT_EQ(h.fields[1].str_val, "a\tb");
  EXPECT_EQ(h.fields[2].str_val, "A"); // U+0041
}

// ``\uXXXX`` escapes for code points above U+007F arrive as UNICODE_CODE_POINT tokens
// and must be encoded to multi-byte UTF-8 by appendCodePoint.
// É (U+00C9) -> 2 UTF-8 bytes C3 89.
// 中 (U+4E2D) -> 3 UTF-8 bytes E4 B8 AD.
TEST(WuffsJsonCursorTest, UnicodeEscapeMultiByteUtf8) {
  CapturingHandler h;
  EXPECT_OK(parse(R"({"a":"\u00C9","b":"\u4E2D"})", h));
  ASSERT_EQ(h.fields.size(), 2u);
  EXPECT_EQ(h.fields[0].str_val, "\xC3\x89");     // U+00C9
  EXPECT_EQ(h.fields[1].str_val, "\xE4\xB8\xAD"); // U+4E2D
}

// Supplementary characters (> U+FFFF) are written in JSON as surrogate pairs
// (``\uHHHH\uLLLL``). Wuffs combines them into a single UNICODE_CODE_POINT token
// with the full code point value; appendCodePoint encodes it as 4-byte UTF-8.
// 😀 -> U+1F600 -> F0 9F 98 80.
TEST(WuffsJsonCursorTest, UnicodeSurrogatePairDecodedToUtf8) {
  CapturingHandler h;
  EXPECT_OK(parse(R"({"a":"\uD83D\uDE00"})", h));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].str_val, "\xF0\x9F\x98\x80"); // U+1F600
}

// Deeper-than-1 content is discarded (openStringCapture returns false).
TEST(WuffsJsonCursorTest, NestedObjectDiscarded) {
  CapturingHandler h;
  EXPECT_OK(parse(R"({"top":"v","nested":{"a":"b"}})", h));
  // "top" is a depth-1 string -> captured.
  // Inner "a"/"b" at depth 2 have openStringCapture return false -> discarded.
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].str_val, "v");
}

// Feed the document in two chunks to verify the Wuffs decoder state persists.
TEST(WuffsJsonCursorTest, StreamingAcrossChunks) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);

  // The string value "gpt-4" straddles the chunk boundary.
  EXPECT_OK(cursor.feed(R"({"model":"gpt)", /*closed=*/false));
  EXPECT_OK(cursor.feed(R"(-4","n":7})", /*closed=*/true));

  ASSERT_EQ(h.fields.size(), 2u);
  EXPECT_EQ(h.fields[0].str_val, "gpt-4");
  EXPECT_EQ(h.fields[1].raw_val, "7");
}

TEST(WuffsJsonCursorTest, InvalidJsonReturnsError) {
  CapturingHandler h;
  EXPECT_THAT(parse("not json", h), Not(IsOk()));
}

TEST(WuffsJsonCursorTest, FeedAfterCompleteReturnsError) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_OK(cursor.feed("{}", /*closed=*/true));
  EXPECT_THAT(cursor.feed("{}", /*closed=*/true), Not(IsOk()));
}

TEST(WuffsJsonCursorTest, TrailingGarbageInSameChunkRejected) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_THAT(cursor.feed(R"({"key":"val"}random_garbage)", /*closed=*/true), Not(IsOk()));
}

TEST(WuffsJsonCursorTest, TrailingGarbageInDifferentDataChunk) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_OK(cursor.feed(R"({"key":"val"})", /*closed=*/true));
  EXPECT_THAT(cursor.feed("random_garbage)", /*closed=*/true), Not(IsOk()));
}

TEST(WuffsJsonCursorTest, DuplicateKeyRejected) {
  CapturingHandler h;
  EXPECT_THAT(parse(R"({"model":"gpt-4","model":"gpt-4o"})", h), Not(IsOk()));
}

TEST(WuffsJsonCursorTest, DuplicateKeyInNestedObjectRejected) {
  CapturingHandler h;
  EXPECT_THAT(parse(R"({"x":{"a":1,"a":2}})", h), Not(IsOk()));
}

// Same key name at different nesting depths must not trigger a false positive.
TEST(WuffsJsonCursorTest, SameKeyNameAtDifferentDepthsAllowed) {
  CapturingHandler h;
  EXPECT_OK(parse(R"({"a":{"a":1}})", h));
}

// Same key name in sibling objects must not trigger a false positive.
TEST(WuffsJsonCursorTest, SameKeyNameInSiblingObjectsAllowed) {
  CapturingHandler h;
  EXPECT_OK(parse(R"([{"a":1},{"a":2}])", h));
}

// Key-length enforcement tests

// Key exactly at kMaxKeyBytes (256) must be accepted.
TEST(WuffsJsonCursorTest, KeyAtMaxBytesAccepted) {
  CapturingHandler h;
  const std::string key(256, 'k');
  EXPECT_OK(parse("{\"" + key + "\":1}", h));
}

// Key one byte over kMaxKeyBytes must be rejected mid-accumulation.
TEST(WuffsJsonCursorTest, KeyExceedsMaxBytesRejected) {
  CapturingHandler h;
  const std::string key(257, 'k');
  EXPECT_THAT(parse("{\"" + key + "\":1}", h), Not(IsOk()));
}

// Same boundary delivered across two chunks: chunk1 ends inside the key (no
// closing quote yet); chunk2 delivers only the closing quote as a DROP STRING
// token. The pre-check must skip DROP tokens -- otherwise the closing quote's
// token_len=1 would push 256+1 over the limit and wrongly reject a valid key.
TEST(WuffsJsonCursorTest, KeyAtMaxBytesAcceptedSplitAcrossChunks) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_OK(cursor.feed("{\"" + std::string(256, 'k'), /*closed=*/false));
  EXPECT_OK(cursor.feed("\":1}", /*closed=*/true));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].key.size(), 256u);
}

// Key whose last byte comes from a \n escape (UNICODE_CODE_POINT token).
// \n is 2 raw source bytes but decodes to 1 UTF-8 byte; the pre-check must
// use the decoded size (1), not token_len (2), or 255+2 would wrongly reject.
TEST(WuffsJsonCursorTest, KeyAtMaxBytesWithEscapeAccepted) {
  CapturingHandler h;
  // 255 plain bytes + \n (1 decoded byte) = 256 bytes total.
  EXPECT_OK(parse("{\"" + std::string(255, 'k') + R"(\n":1})", h));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].key.size(), 256u);
}

// 256 plain bytes + \n (1 decoded byte) = 257 bytes -- must be rejected.
TEST(WuffsJsonCursorTest, KeyExceedsMaxBytesWithEscapeRejected) {
  CapturingHandler h;
  EXPECT_THAT(parse("{\"" + std::string(256, 'k') + R"(\n":1})", h), Not(IsOk()));
}

// Handler abort propagation

// A non-OK status returned from onNumber must cause feed() to return that error.
TEST(WuffsJsonCursorTest, HandlerAbortPropagated) {
  AbortOnNumberHandler h;
  EXPECT_THAT(parse(R"({"n":1})", h), Not(IsOk()));
}

// A non-OK status returned from onBoolean must cause feed() to return that error.
TEST(WuffsJsonCursorTest, BooleanHandlerAbortPropagated) {
  class AbortOnBoolHandler : public CapturingHandler {
  public:
    absl::Status onBoolean(absl::string_view, bool, int, size_t, size_t) override {
      return absl::InternalError("bool abort");
    }
  } h;
  EXPECT_THAT(parse(R"({"ok":true})", h), Not(IsOk()));
}

// A non-OK status returned from onKey must cause feed() to return that error.
TEST(WuffsJsonCursorTest, KeyHandlerAbortPropagated) {
  class AbortOnKeyHandler : public CapturingHandler {
  public:
    absl::Status onKey(absl::string_view, int, size_t) override {
      return absl::InternalError("key abort");
    }
  } h;
  EXPECT_THAT(parse(R"({"a":1})", h), Not(IsOk()));
}

// onStringChunk early-abort tests
//
// A handler returning false from onStringChunk must stop further chunk delivery
// but must not suppress closeStringCapture.

class AbortStringChunkHandler : public WuffsJsonCursor::Handler {
public:
  bool close_fired{false};

  bool openStringCapture(absl::string_view, int, size_t) override { return true; }
  bool onStringChunk(absl::string_view, int, absl::string_view) override { return false; }
  void closeStringCapture(absl::string_view, int, size_t) override { close_fired = true; }
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

// Returning false on a STRING COPY token stops further chunk delivery
// but closeStringCapture still fires.
TEST(WuffsJsonCursorTest, OnStringChunkFalseStopsDeliveryOnCopy) {
  AbortStringChunkHandler h;
  EXPECT_OK(parse(R"({"a":"hello"})", h));
  EXPECT_TRUE(h.close_fired);
}

// Returning false on a UNICODE_CODE_POINT token (escape at the start of the value)
// stops further chunk delivery but closeStringCapture still fires.
TEST(WuffsJsonCursorTest, OnStringChunkFalseStopsDeliveryOnEscape) {
  AbortStringChunkHandler h;
  EXPECT_OK(parse(R"({"a":"\nhello"})", h));
  EXPECT_TRUE(h.close_fired);
}

// Post-completion feed test

// Calling feed() after the document is fully consumed must return InvalidArgumentError
// via the wuffs_done_ guard without re-processing.
TEST(WuffsJsonCursorTest, FeedAfterCompletion) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_OK(cursor.feed("{}", true));
  EXPECT_THAT(cursor.feed("extra", true), Not(IsOk()));
  EXPECT_TRUE(h.fields.empty());
}

// Trailing whitespace tests

TEST(WuffsJsonCursorTest, TrailingNewlineAccepted) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_OK(cursor.feed("{}\n", true));
}

TEST(WuffsJsonCursorTest, TrailingSpacesAccepted) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_OK(cursor.feed("[]  \n", true));
}

// nextSourcePosition test
TEST(WuffsJsonCursorTest, NextSourcePositionAfterParse) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  constexpr absl::string_view json = R"({"a":1})";
  EXPECT_OK(cursor.feed(json, true));
  EXPECT_EQ(cursor.nextSourcePosition(), json.size());
}

// Token buffer reset test

// 50 key-value pairs generate ~301 tokens in a single feed() call, exceeding the
// 256-token ring buffer (kTokenBufLen) and forcing the short_write reset path.
TEST(WuffsJsonCursorTest, LargeInputTriggersTokenBufferReset) {
  CapturingHandler h;
  std::string json = "{";
  for (int i = 0; i < 50; ++i) {
    if (i > 0) {
      json += ",";
    }
    json += "\"k" + std::to_string(i) + "\":" + std::to_string(i);
  }
  json += "}";
  EXPECT_OK(parse(json, h));
  EXPECT_EQ(h.fields.size(), 50u);
}

// Byte-range offset tests

// The root container's [token_start, token_end) byte range must cover the full input.
TEST(WuffsJsonCursorTest, ByteRangeContainerCoversWholeInput) {
  constexpr absl::string_view json = R"({"a":1})";
  ByteRangeHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_OK(cursor.feed(json, /*closed=*/true));
  EXPECT_EQ(h.container_open_start, 0u);
  EXPECT_EQ(h.container_close_end, json.size());
}

// [onKey.token_start, value.token_end) must cover the complete "key":value field
// verbatim, suitable for zero-copy passthrough.
TEST(WuffsJsonCursorTest, ByteRangeKeyValueField) {
  constexpr absl::string_view json = R"({"a":1})";
  ByteRangeHandler h;
  WuffsJsonCursor cursor(h);
  EXPECT_OK(cursor.feed(json, /*closed=*/true));
  ASSERT_NE(h.first_key_start, ByteRangeHandler::kNotSet);
  ASSERT_NE(h.first_value_end, ByteRangeHandler::kNotSet);
  EXPECT_EQ(json.substr(h.first_key_start, h.first_value_end - h.first_key_start), R"("a":1)");
}

// closeStringCapture must fire even when openStringCapture returned false, so that
// passthrough handlers can get [token_start, token_end) for string values without
// paying the cost of content decoding.
TEST(WuffsJsonCursorTest, CloseStringCaptureFiresWhenOpenReturnedFalse) {
  class PassthroughRangeHandler : public WuffsJsonCursor::Handler {
  public:
    size_t open_start{size_t(-1)};
    size_t close_end{size_t(-1)};

    bool openStringCapture(absl::string_view, int, size_t ts) override {
      open_start = ts;
      return false;
    }
    bool onStringChunk(absl::string_view, int, absl::string_view) override { return true; }
    void closeStringCapture(absl::string_view, int, size_t te) override { close_end = te; }
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
  } h;

  constexpr absl::string_view json = R"({"a":"hello"})";
  EXPECT_OK(parse(json, h));
  ASSERT_NE(h.open_start, size_t(-1));
  ASSERT_NE(h.close_end, size_t(-1));
  // [open_start, close_end) must cover the verbatim string token "hello".
  EXPECT_EQ(json.substr(h.open_start, h.close_end - h.open_start), R"("hello")");
}

// ByteRangeHandler returns false from openStringCapture; after the fix,
// closeStringCapture still fires, so first_value_end is populated for string values.
TEST(WuffsJsonCursorTest, ByteRangeKeyValueFieldString) {
  constexpr absl::string_view json = R"({"a":"hello"})";
  ByteRangeHandler h;
  EXPECT_OK(parse(json, h));
  ASSERT_NE(h.first_key_start, ByteRangeHandler::kNotSet);
  ASSERT_NE(h.first_value_end, ByteRangeHandler::kNotSet);
  EXPECT_EQ(json.substr(h.first_key_start, h.first_value_end - h.first_key_start),
            R"("a":"hello")");
}

// Depth limit tests

// 8 levels of nesting must be accepted (boundary value for default max_depth=8).
TEST(WuffsJsonCursorTest, MaxDepthAccepted) {
  CapturingHandler h;
  // 8 nested objects, scalar at the innermost level.
  EXPECT_OK(parse(R"({"a":{"a":{"a":{"a":{"a":{"a":{"a":{"a":1}}}}}}}})", h));
}

// 9 levels of nesting must be rejected (fail-closed).
TEST(WuffsJsonCursorTest, ExceedMaxDepthRejected) {
  CapturingHandler h;
  EXPECT_THAT(parse(R"({"a":{"a":{"a":{"a":{"a":{"a":{"a":{"a":{"a":1}}}}}}}}})", h), Not(IsOk()));
}

// Path-tracking tests

// Scalar elements in an array must each get a distinct 0-based index.
// Before the fix, array_index_ was only incremented on container close, so
// [1,2,3] would report [0] for every element.
TEST(WuffsJsonCursorTest, BuildIndexedPathScalarsInArray) {
  PathCapturingHandler h;
  EXPECT_OK(parsePaths("[1, 2, 3]", h));
  ASSERT_EQ(h.paths.size(), 3u);
  EXPECT_EQ(h.paths[0], "[0]");
  EXPECT_EQ(h.paths[1], "[1]");
  EXPECT_EQ(h.paths[2], "[2]");
}

// String elements in an array must also advance the index counter.
TEST(WuffsJsonCursorTest, BuildIndexedPathStringsInArray) {
  PathCapturingHandler h;
  EXPECT_OK(parsePaths(R"(["a","b","c"])", h));
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
  EXPECT_OK(parsePaths(R"([1, {"a": 2}])", h));
  ASSERT_EQ(h.paths.size(), 2u);
  EXPECT_EQ(h.paths[0], "[0]");   // scalar 1 at depth 1
  EXPECT_EQ(h.paths[1], "[1].a"); // number 2 at depth 2, inside second element
}

// Typical LLM request shape: messages array of role/content objects.
// Each object's string value must be reported under the correct array index.
TEST(WuffsJsonCursorTest, BuildIndexedPathNestedMessages) {
  PathCapturingHandler h;
  EXPECT_OK(parsePaths(R"({"messages":[{"role":"user"},{"role":"assistant"}]})", h));
  ASSERT_EQ(h.paths.size(), 2u);
  EXPECT_EQ(h.paths[0], "messages[0].role");
  EXPECT_EQ(h.paths[1], "messages[1].role");
}

// buildPatternPath must use [] instead of [n] and be identical for each element.
TEST(WuffsJsonCursorTest, BuildPatternPathNestedMessages) {
  PatternCapturingHandler h;
  WuffsJsonCursor cursor(h, /*track_paths=*/true);
  h.cursor = &cursor;
  EXPECT_OK(cursor.feed(R"({"messages":[{"role":"user"},{"role":"assistant"}]})",
                        /*closed=*/true));
  ASSERT_EQ(h.paths.size(), 2u);
  EXPECT_EQ(h.paths[0], "messages[].role");
  EXPECT_EQ(h.paths[1], "messages[].role");
}

// NUMBER split test, "1234" split as "12" | "34":
TEST(WuffsJsonCursorTest, NumberSplitChunkCorrectValue) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  ASSERT_OK(cursor.feed(R"({"n":12)", /*closed=*/false));
  ASSERT_OK(cursor.feed(R"(34})", /*closed=*/true));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].raw_val, "1234");
}

// LITERAL split test
TEST(WuffsJsonCursorTest, LiteralSplitChunkAccepted) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  ASSERT_OK(cursor.feed(R"({"x":tr)", /*closed=*/false));
  EXPECT_OK(cursor.feed(R"(ue})", /*closed=*/true));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].raw_val, "true");
}

// STRING value split across a chunk boundary: "hello" as {"s":"hel" | lo"}.
// Wuffs emits what it has as a continued=true STRING token before suspending,
// leaving no unread bytes. The next chunk delivers the remainder cleanly.
TEST(WuffsJsonCursorTest, StringValueSplitChunkCorrectValue) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  ASSERT_OK(cursor.feed(R"({"s":"hel)", /*closed=*/false));
  ASSERT_OK(cursor.feed(R"(lo"})", /*closed=*/true));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].str_val, "hello");
}

// NUMBER split across three chunks: "123456" as "12" | "34" | "56".
// Exercises the compound case where pending_bytes_ is rebuilt twice: chunk1
// sets pending_bytes_="12", chunk2 stitches "1234" but Wuffs still can't
// complete the NUMBER (no closing delimiter), chunk3 closes it.
TEST(WuffsJsonCursorTest, NumberSplitThreeChunksCorrectValue) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  ASSERT_OK(cursor.feed(R"({"n":12)", /*closed=*/false));
  ASSERT_OK(cursor.feed(R"(34)", /*closed=*/false));
  ASSERT_OK(cursor.feed(R"(56})", /*closed=*/true));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].raw_val, "123456");
}

// ``\uXXXX`` escape straddling a chunk boundary within a string value.
// Wuffs handles mid-escape suspension via coroutine state (no pending_bytes_
// involved, unlike NUMBER/LITERAL splits): the decoder consumes all available
// bytes including the \u prefix and resumes on the next chunk from the hex
// digits. U+0041 = 'A', so "a\u" | "0041b" must decode to "aAb".
TEST(WuffsJsonCursorTest, UnicodeEscapeSplitChunkCorrectValue) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  ASSERT_OK(cursor.feed(R"({"s":"a\u)", /*closed=*/false));
  ASSERT_OK(cursor.feed(R"(0041b"})", /*closed=*/true));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].str_val, "aAb"); // U+0041 = 'A'
}

// A NUMBER token that straddles a chunk boundary causes Wuffs to rewind its
// read cursor and the cursor to save the unread bytes in pending_bytes_.
// If those bytes exceed kMaxPendingBytes the cursor must reject immediately.

// A number whose in-flight byte count equals kMaxPendingBytes (64) must not
// trigger the cap: the next chunk completes it normally.
TEST(WuffsJsonCursorTest, NumberAtPendingCapAccepted) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  // Feed structure first; no number in flight yet.
  ASSERT_OK(cursor.feed("{\"n\":", /*closed=*/false));
  // Feed exactly 64 digit bytes with no terminator — pending_bytes_ = 64, not over cap.
  ASSERT_OK(cursor.feed(std::string(64, '1'), /*closed=*/false));
  // Close with '}': Wuffs now sees terminator and completes the NUMBER token.
  ASSERT_OK(cursor.feed("}", /*closed=*/true));
  ASSERT_EQ(h.fields.size(), 1u);
  EXPECT_EQ(h.fields[0].raw_val, std::string(64, '1'));
}

// A number with 65 in-flight bytes (one over kMaxPendingBytes) must be rejected.
TEST(WuffsJsonCursorTest, NumberOverPendingCapRejected) {
  CapturingHandler h;
  WuffsJsonCursor cursor(h);
  ASSERT_OK(cursor.feed("{\"n\":", /*closed=*/false));
  // 65 digit bytes with no terminator — leftover = 65 > 64 → error.
  EXPECT_THAT(cursor.feed(std::string(65, '1'), /*closed=*/false), Not(IsOk()));
}

} // namespace
} // namespace Wuffs
} // namespace Json
} // namespace Envoy
