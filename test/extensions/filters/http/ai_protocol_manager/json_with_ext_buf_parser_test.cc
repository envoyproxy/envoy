#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf_parser.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::HasStatusCode;

using ExternalRef = JsonWithExtBuf::ExternalRef;

class JsonWithExtBufParserTest : public testing::Test {
public:
  // Small so test bodies stay readable.
  static constexpr std::uint32_t kThreshold = 8;

  JsonWithExtBufParserTest() { config_.inline_string_threshold_bytes = kThreshold; }

  // Parses `body` in one shot, keeping the resulting document.
  absl::Status parse(absl::string_view body) {
    JsonWithExtBufParser parser(config_);
    absl::Status status = parser.feed(body, /*end_stream=*/true);
    doc_ = parser.takeDocument();
    return status;
  }

  // Feeds `body` `chunk_size` bytes at a time, splitting tokens across feeds.
  absl::Status parseChunked(absl::string_view body, size_t chunk_size) {
    JsonWithExtBufParser parser(config_);
    for (size_t i = 0; i < body.size(); i += chunk_size) {
      const size_t len = std::min(chunk_size, body.size() - i);
      const bool last = (i + len == body.size());
      if (auto status = parser.feed(body.substr(i, len), last); !status.ok()) {
        return status;
      }
    }
    doc_ = parser.takeDocument();
    return absl::OkStatus();
  }

  const nlohmann::json& json() { return doc_.json(); }

  // Decodes the reference at `node`, failing the test if it is not one.
  ExternalRef refAt(const nlohmann::json& node) {
    const absl::StatusOr<ExternalRef> ref = JsonWithExtBuf::externalRef(node);
    EXPECT_TRUE(ref.ok()) << ref.status();
    return ref.ok() ? *ref : ExternalRef{};
  }

  JsonWithExtBufParser::Config config_;
  JsonWithExtBuf doc_;
};

// Everything small enough stays in the DOM, with types preserved.
TEST_F(JsonWithExtBufParserTest, InlinesSmallValues) {
  ASSERT_OK(parse(R"({"model":"gpt-4","n":2,"temp":0.5,"stream":true,"stop":null})"));

  EXPECT_EQ(json()["model"], "gpt-4");
  EXPECT_TRUE(json()["n"].is_number_integer());
  EXPECT_EQ(json()["n"], 2);
  EXPECT_TRUE(json()["temp"].is_number_float());
  EXPECT_DOUBLE_EQ(json()["temp"].get<double>(), 0.5);
  EXPECT_EQ(json()["stream"], true);
  EXPECT_TRUE(json()["stop"].is_null());
}

TEST_F(JsonWithExtBufParserTest, BuildsNestedContainers) {
  ASSERT_OK(parse(R"({"messages":[{"role":"user"},{"role":"tool","ids":[1,2,3]}],"tools":[]})"));

  ASSERT_TRUE(json()["messages"].is_array());
  EXPECT_EQ(json()["messages"].size(), 2);
  EXPECT_EQ(json()["messages"][0]["role"], "user");
  EXPECT_EQ(json()["messages"][1]["ids"], nlohmann::json::array({1, 2, 3}));
  EXPECT_TRUE(json()["tools"].is_array());
  EXPECT_TRUE(json()["tools"].empty());
}

// An oversized string is not in the DOM; the reference left in its place
// locates its bytes in the body.
TEST_F(JsonWithExtBufParserTest, OffloadsOversizedString) {
  const std::string prompt(4096, 'x');
  const std::string body = absl::StrCat(R"({"model":"gpt-4","prompt":")", prompt, R"("})");

  ASSERT_OK(parse(body));

  EXPECT_EQ(json()["model"], "gpt-4");
  ASSERT_TRUE(JsonWithExtBuf::isExternalRef(json()["prompt"]));

  const ExternalRef ref = refAt(json()["prompt"]);
  EXPECT_EQ(ref.length, prompt.size());
  // The recorded range is the content between the quotes -- not including them.
  EXPECT_EQ(body.substr(ref.offset, ref.length), prompt);
  EXPECT_EQ(body[ref.offset - 1], '"');
  EXPECT_EQ(body[ref.offset + ref.length], '"');
}

// A string at exactly the threshold is inline; one byte more is offloaded.
TEST_F(JsonWithExtBufParserTest, ThresholdBoundary) {
  const std::string at_limit(kThreshold, 'a');
  ASSERT_OK(parse(absl::StrCat(R"({"p":")", at_limit, R"("})")));
  EXPECT_EQ(json()["p"], at_limit);

  const std::string over_limit(kThreshold + 1, 'a');
  ASSERT_OK(parse(absl::StrCat(R"({"p":")", over_limit, R"("})")));
  EXPECT_TRUE(JsonWithExtBuf::isExternalRef(json()["p"]));
}

// Keys are never offloaded, however long the value they introduce is.
TEST_F(JsonWithExtBufParserTest, KeysAreAlwaysInline) {
  const std::string long_key(64, 'k');
  ASSERT_OK(parse(absl::StrCat(R"({")", long_key, R"(":"short"})")));
  EXPECT_EQ(json()[long_key], "short");
}

// The range covers the raw, still-escaped bytes, so its length is the
// on-the-wire length rather than the decoded one.
TEST_F(JsonWithExtBufParserTest, OffloadedRangeIsRawEscapedBytes) {
  // 16 plain bytes + a 2-char \n escape + a 6-char é escape + 1: 25 on the wire,
  // 20 decoded.
  const std::string raw_content = absl::StrCat(std::string(16, 'a'), R"(\n\u00e9)", "b");
  ASSERT_EQ(raw_content.size(), 25);
  const std::string body = absl::StrCat(R"({"p":")", raw_content, R"("})");

  ASSERT_OK(parse(body));

  const ExternalRef ref = refAt(json()["p"]);
  EXPECT_EQ(ref.length, raw_content.size());
  EXPECT_EQ(body.substr(ref.offset, ref.length), raw_content);
}

// Offsets are absolute over the body, so a value split across feeds still lands
// on the right bytes.
TEST_F(JsonWithExtBufParserTest, ChunkedFeedMatchesSingleShot) {
  const std::string prompt(300, 'p');
  const std::string body =
      absl::StrCat(R"({"messages":[{"role":"user","content":")", prompt,
                   R"("}],"max_tokens":1024,"temp":-0.25,"nested":{"deep":[null,true,"tiny"]}})");

  ASSERT_OK(parse(body));
  const nlohmann::json single_shot = json();

  for (const size_t chunk_size : std::vector<size_t>{1, 7, 64, 4096}) {
    ASSERT_OK(parseChunked(body, chunk_size)) << "chunk size " << chunk_size;
    EXPECT_EQ(json(), single_shot) << "chunk size " << chunk_size;
  }

  const ExternalRef ref = refAt(single_shot["messages"][0]["content"]);
  EXPECT_EQ(body.substr(ref.offset, ref.length), prompt);
}

// End to end: the recorded reference is enough to fetch exactly that value's
// bytes back out of an ExternalBuffer holding the offloaded body.
TEST_F(JsonWithExtBufParserTest, ReferenceResolvesAgainstTheOffloadedBody) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("test");
  InMemoryExternalBuffer buffer(*dispatcher);

  const std::string prompt(2048, 'z');
  const std::string body =
      absl::StrCat(R"({"model":"gpt-4","messages":[{"content":")", prompt, R"("}]})");

  // Offload as the BufferManager does -- verbatim from offset 0 -- which is what
  // makes the recorded offsets valid buffer offsets.
  buffer.write(std::make_unique<Buffer::OwnedImpl>(body),
               [](ExternalBufferStatus status) { EXPECT_EQ(status, ExternalBufferStatus::Ok); });
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_EQ(buffer.length(), body.size());

  ASSERT_OK(parse(body));
  const absl::StatusOr<ExternalRef> ref =
      JsonWithExtBuf::externalRef(json()["messages"][0]["content"]);
  ASSERT_OK(ref);

  std::string fetched;
  buffer.read(ref->offset, ref->length,
              [&fetched](ExternalBufferStatus status, Buffer::InstancePtr data) {
                ASSERT_EQ(status, ExternalBufferStatus::Ok);
                fetched = data->toString();
              });
  EXPECT_EQ(fetched, prompt);
}

TEST_F(JsonWithExtBufParserTest, RootScalarAndRootArray) {
  ASSERT_OK(parse(R"("hello")"));
  EXPECT_EQ(json(), "hello");

  ASSERT_OK(parse("[1,2,3]"));
  EXPECT_EQ(json(), nlohmann::json::array({1, 2, 3}));

  ASSERT_OK(parse("42"));
  EXPECT_EQ(json(), 42);
}

TEST_F(JsonWithExtBufParserTest, NumberTypes) {
  ASSERT_OK(parse(R"({"i":-17,"big":18446744073709551615,"f":1.5e3})"));

  EXPECT_TRUE(json()["i"].is_number_integer());
  EXPECT_EQ(json()["i"], -17);
  EXPECT_TRUE(json()["big"].is_number_unsigned());
  EXPECT_EQ(json()["big"].get<std::uint64_t>(), 18446744073709551615ULL);
  EXPECT_DOUBLE_EQ(json()["f"].get<double>(), 1500.0);
}

// A literal no double can hold would serialize back as null; reject instead.
TEST_F(JsonWithExtBufParserTest, UnrepresentableNumberIsRejected) {
  EXPECT_THAT(parse(R"({"n":1e400})"), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

// A body that simply stops has no malformed token; the completeness check
// catches it.
TEST_F(JsonWithExtBufParserTest, TruncatedDocumentIsRejected) {
  EXPECT_THAT(parse(R"({"model":"gpt-4")"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(parse(R"({"messages":[{"role":)"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(parse(""), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST_F(JsonWithExtBufParserTest, MalformedDocumentIsRejected) {
  EXPECT_THAT(parse(R"({"model" "gpt-4"})"), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(parse(R"({"a":1}trailing)"), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

// Pinned because the point of parsing in the proxy is that Envoy and the backend
// must not be able to read the same body differently.
TEST_F(JsonWithExtBufParserTest, DuplicateKeyIsRejected) {
  EXPECT_THAT(parse(R"({"model":"a","model":"b"})"),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST_F(JsonWithExtBufParserTest, FeedAfterCompletionIsRejected) {
  JsonWithExtBufParser parser(config_);
  ASSERT_OK(parser.feed(R"({"a":1})", /*end_stream=*/true));

  EXPECT_THAT(parser.feed(R"({"b":2})", /*end_stream=*/true),
              HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

// Errors are sticky: the same failure comes back rather than half a document.
TEST_F(JsonWithExtBufParserTest, ErrorsAreTerminal) {
  JsonWithExtBufParser parser(config_);
  EXPECT_THAT(parser.feed(R"({"a" 1})", /*end_stream=*/false),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(parser.feed(R"(})", /*end_stream=*/true),
              HasStatusCode(absl::StatusCode::kInvalidArgument));

  // Nothing was published into the document.
  EXPECT_TRUE(parser.takeDocument().json().is_null());
}

// Published only once complete, so a caller cannot act on a partial DOM.
TEST_F(JsonWithExtBufParserTest, DocumentIsPublishedOnlyOnCompletion) {
  JsonWithExtBufParser parser(config_);

  ASSERT_OK(parser.feed(R"({"model":"gpt-4",)", /*end_stream=*/false));

  ASSERT_OK(parser.feed(R"("n":1})", /*end_stream=*/true));
  const JsonWithExtBuf doc = parser.takeDocument();
  EXPECT_EQ(doc.json()["model"], "gpt-4");
  EXPECT_EQ(doc.json()["n"], 1);
}

// The cursor callbacks below are driven directly. They are the parser's public
// Handler surface, but only the cursor is meant to call them -- and a correct
// cursor never emits these sequences. The parser guards against them anyway,
// because a callback that cannot return a status has nowhere else to put a
// failure: it records the first one and drops everything after it, and feed()
// is what surfaces it.

// The stack is only ever popped by a close the cursor reports, so a close with
// nothing open means the callback sequence is broken, not the body.
TEST_F(JsonWithExtBufParserTest, ContainerCloseWithNothingOpenIsRejected) {
  JsonWithExtBufParser parser(config_);
  parser.onContainerClose(/*depth=*/1, /*token_end=*/0);

  EXPECT_THAT(parser.feed(R"({"a":1})", /*end_stream=*/true),
              HasStatusCode(absl::StatusCode::kInternal));
  EXPECT_TRUE(parser.takeDocument().json().is_null());
}

// Whatever the cursor reports next, none of it is built: the callbacks stop
// short of the DOM, and captured string content is dropped rather than
// accumulated for a value that will never be attached.
TEST_F(JsonWithExtBufParserTest, CallbacksNoOpOnceAnErrorIsRecorded) {
  JsonWithExtBufParser parser(config_);
  parser.onContainerClose(/*depth=*/1, /*token_end=*/0);

  EXPECT_FALSE(parser.openStringCapture("s", /*depth=*/1, /*token_start=*/0));
  EXPECT_FALSE(parser.onStringChunk("s", /*depth=*/1, "abc"));
  parser.closeStringCapture("s", /*depth=*/1, /*token_end=*/5);
  parser.onNull("n", /*depth=*/1, /*token_start=*/0, /*token_end=*/4);
  parser.onContainerOpen("c", /*is_dict=*/true, /*depth=*/1, /*token_start=*/0);
  parser.onContainerClose(/*depth=*/1, /*token_end=*/1);

  EXPECT_THAT(parser.feed("{}", /*end_stream=*/true), HasStatusCode(absl::StatusCode::kInternal));
  EXPECT_TRUE(parser.takeDocument().json().is_null());
}

// Trailing bytes are rejected by the cursor, so a value can only follow the
// root if the callback sequence is broken; the first root is not overwritten.
TEST_F(JsonWithExtBufParserTest, SecondRootValueIsRejected) {
  JsonWithExtBufParser parser(config_);
  parser.onNull("", /*depth=*/1, /*token_start=*/0, /*token_end=*/4);

  // The error is recorded part way through the feed, where the callback has no
  // status to return; the cursor finishes the body happily, and feed() reports
  // the recorded error rather than the cursor's success.
  EXPECT_THAT(parser.feed(R"({"a":1})", /*end_stream=*/true),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(parser.takeDocument().json().is_null());
}

// A reference is only as good as the offsets behind it, so a range that cannot
// even span the two quotes is refused rather than recorded.
TEST_F(JsonWithExtBufParserTest, OffloadedStringWithImpossibleRangeIsRejected) {
  JsonWithExtBufParser parser(config_);
  // Past the threshold, so the value is headed for a reference rather than the
  // DOM -- the only path that uses the token range.
  ASSERT_OK(parser.feed(absl::StrCat(R"({"s":")", std::string(kThreshold + 1, 'a')),
                        /*end_stream=*/false));

  parser.closeStringCapture("s", /*depth=*/2, /*token_end=*/0);

  EXPECT_THAT(parser.feed(R"("})", /*end_stream=*/true),
              HasStatusCode(absl::StatusCode::kInternal));
}

// The cursor is what closes containers, so if it ever reports a document
// complete with one still open, the DOM is unfinished and is not published.
TEST_F(JsonWithExtBufParserTest, ContainerLeftOpenAtEndStreamIsRejected) {
  JsonWithExtBufParser parser(config_);
  parser.onContainerOpen("", /*is_dict=*/false, /*depth=*/1, /*token_start=*/0);

  EXPECT_THAT(parser.feed("1", /*end_stream=*/true),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(parser.takeDocument().json().is_null());
}

// A realistic shape: oversized message content each getting its own reference,
// with small metadata left inline.
TEST_F(JsonWithExtBufParserTest, MixedInlineAndOffloadedValues) {
  const std::string system(600, 's');
  const std::string user(900, 'u');
  const std::string body =
      absl::StrCat(R"({"model":"gpt-4","messages":[{"role":"system","content":")", system,
                   R"("},{"role":"user","content":")", user, R"("}],"max_tokens":256})");

  ASSERT_OK(parse(body));

  EXPECT_EQ(json()["model"], "gpt-4");
  EXPECT_EQ(json()["max_tokens"], 256);
  EXPECT_EQ(json()["messages"][0]["role"], "system");
  EXPECT_EQ(json()["messages"][1]["role"], "user");

  const ExternalRef system_ref = refAt(json()["messages"][0]["content"]);
  const ExternalRef user_ref = refAt(json()["messages"][1]["content"]);
  EXPECT_EQ(body.substr(system_ref.offset, system_ref.length), system);
  EXPECT_EQ(body.substr(user_ref.offset, user_ref.length), user);
  EXPECT_LT(system_ref.offset, user_ref.offset);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
