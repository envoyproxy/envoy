#include "source/extensions/filters/http/mcp_json_rest_bridge/sse_response_extractor.h"

#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::Envoy::StatusHelpers::StatusIs;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

class SseResponseExtractorTest : public testing::Test {
protected:
  SseResponseExtractor extractor_;
};

TEST_F(SseResponseExtractorTest, ProcessSingleCompleteEvent) {
  EXPECT_THAT(*extractor_.processChunk("data: hello world\n\n", /*end_stream=*/false),
              ElementsAre("hello world"));
}

TEST_F(SseResponseExtractorTest, ProcessMultipleEvents) {
  EXPECT_THAT(
      *extractor_.processChunk("data: first event\n\ndata: second event\n\n", /*end_stream=*/false),
      ElementsAre("first event", "second event"));
}

TEST_F(SseResponseExtractorTest, ProcessIncompleteEvent) {
  // First chunk has incomplete event, should return nothing.
  EXPECT_THAT(*extractor_.processChunk("data: first", /*end_stream=*/false), IsEmpty());

  // Second chunk completes the event.
  EXPECT_THAT(*extractor_.processChunk(" event\n\n", /*end_stream=*/false),
              ElementsAre("first event"));
}

TEST_F(SseResponseExtractorTest, ProcessMultipleChunks) {
  EXPECT_THAT(*extractor_.processChunk("da", /*end_stream=*/false), IsEmpty());
  EXPECT_THAT(*extractor_.processChunk("ta: hello", /*end_stream=*/false), IsEmpty());
  EXPECT_THAT(*extractor_.processChunk(" world\n", /*end_stream=*/false), IsEmpty());
  EXPECT_THAT(*extractor_.processChunk("\n", /*end_stream=*/false), ElementsAre("hello world"));
}

TEST_F(SseResponseExtractorTest, ProcessCommentsOnly) {
  EXPECT_THAT(*extractor_.processChunk(": this is a comment\n\n", /*end_stream=*/false), IsEmpty());
}

TEST_F(SseResponseExtractorTest, ProcessNoDataEvent) {
  EXPECT_THAT(*extractor_.processChunk("event: ping\nid: 123\n\n", /*end_stream=*/false),
              IsEmpty());
}

TEST_F(SseResponseExtractorTest, ProcessCRLFLineEndings) {
  EXPECT_THAT(*extractor_.processChunk("data: crlf test\r\n\r\n", /*end_stream=*/false),
              ElementsAre("crlf test"));
}

TEST_F(SseResponseExtractorTest, ProcessEndStreamWithIncompleteEvent) {
  // Without end_stream, incomplete event is buffered.
  EXPECT_THAT(*extractor_.processChunk("data: last event", /*end_stream=*/false), IsEmpty());
  EXPECT_THAT(*extractor_.processChunk("", /*end_stream=*/true), IsEmpty());
}

TEST_F(SseResponseExtractorTest, ProcessMultilineData) {
  EXPECT_THAT(*extractor_.processChunk("data: line one\ndata: line two\n\n", /*end_stream=*/false),
              ElementsAre("line one\nline two"));
}

TEST_F(SseResponseExtractorTest, ProcessJSONPayload) {
  EXPECT_THAT(*extractor_.processChunk("data: {\"foo\": \"bar\"}\n\n", /*end_stream=*/false),
              ElementsAre("{\"foo\": \"bar\"}"));
}

TEST_F(SseResponseExtractorTest, ProcessEmptyChunk) {
  EXPECT_THAT(*extractor_.processChunk("", /*end_stream=*/false), IsEmpty());
}

TEST_F(SseResponseExtractorTest, ProcessMixedEvents) {
  EXPECT_THAT(*extractor_.processChunk("event: ping\n\ndata: hello\n\n: comment\n\ndata: world\n\n",
                                       /*end_stream=*/false),
              ElementsAre("hello", "world"));
}

TEST(SseResponseExtractorLimitTest, EnforcesLimit) {
  SseResponseExtractor limited_extractor(10);
  // First chunk is fine (9 bytes)
  EXPECT_THAT(*limited_extractor.processChunk("data: foo", /*end_stream=*/false), IsEmpty());

  // Second chunk exceeds the limit (9 + 2 = 11 > 10)
  EXPECT_THAT(limited_extractor.processChunk("\n\n", /*end_stream=*/false),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
