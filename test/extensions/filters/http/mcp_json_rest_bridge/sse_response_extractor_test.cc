#include "source/extensions/filters/http/mcp_json_rest_bridge/sse_response_extractor.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

class SseResponseExtractorTest : public testing::Test {
protected:
  SseResponseExtractor extractor_;
};

TEST_F(SseResponseExtractorTest, ProcessSingleCompleteEvent) {
  auto payloads = extractor_.processChunk("data: hello world\n\n");
  EXPECT_THAT(payloads, ElementsAre("hello world"));
}

TEST_F(SseResponseExtractorTest, ProcessMultipleEvents) {
  auto payloads = extractor_.processChunk("data: first event\n\ndata: second event\n\n");
  EXPECT_THAT(payloads, ElementsAre("first event", "second event"));
}

TEST_F(SseResponseExtractorTest, ProcessIncompleteEvent) {
  // First chunk has incomplete event, should return nothing.
  auto payloads1 = extractor_.processChunk("data: first");
  EXPECT_THAT(payloads1, IsEmpty());

  // Second chunk completes the event.
  auto payloads2 = extractor_.processChunk(" event\n\n");
  EXPECT_THAT(payloads2, ElementsAre("first event"));
}

TEST_F(SseResponseExtractorTest, ProcessMultipleChunks) {
  EXPECT_THAT(extractor_.processChunk("da"), IsEmpty());
  EXPECT_THAT(extractor_.processChunk("ta: hello"), IsEmpty());
  EXPECT_THAT(extractor_.processChunk(" world\n"), IsEmpty());
  EXPECT_THAT(extractor_.processChunk("\n"), ElementsAre("hello world"));
}

TEST_F(SseResponseExtractorTest, ProcessCommentsOnly) {
  auto payloads = extractor_.processChunk(": this is a comment\n\n");
  EXPECT_THAT(payloads, IsEmpty());
}

TEST_F(SseResponseExtractorTest, ProcessNoDataEvent) {
  auto payloads = extractor_.processChunk("event: ping\nid: 123\n\n");
  EXPECT_THAT(payloads, IsEmpty());
}

TEST_F(SseResponseExtractorTest, ProcessCRLFLineEndings) {
  auto payloads = extractor_.processChunk("data: crlf test\r\n\r\n");
  EXPECT_THAT(payloads, ElementsAre("crlf test"));
}

TEST_F(SseResponseExtractorTest, ProcessEndStreamWithIncompleteEvent) {
  // Without end_stream, incomplete event is buffered.
  EXPECT_THAT(extractor_.processChunk("data: last event"), IsEmpty());

  EXPECT_THAT(extractor_.processChunk("", /*end_stream=*/true), IsEmpty());
}

TEST_F(SseResponseExtractorTest, ProcessMultilineData) {
  auto payloads = extractor_.processChunk("data: line one\ndata: line two\n\n");
  EXPECT_THAT(payloads, ElementsAre("line one\nline two"));
}

TEST_F(SseResponseExtractorTest, ProcessJSONPayload) {
  auto payloads = extractor_.processChunk("data: {\"foo\": \"bar\"}\n\n");
  EXPECT_THAT(payloads, ElementsAre("{\"foo\": \"bar\"}"));
}

TEST_F(SseResponseExtractorTest, ProcessEmptyChunk) {
  auto payloads = extractor_.processChunk("");
  EXPECT_THAT(payloads, IsEmpty());
}

TEST_F(SseResponseExtractorTest, ProcessMixedEvents) {
  auto payloads =
      extractor_.processChunk("event: ping\n\ndata: hello\n\n: comment\n\ndata: world\n\n");
  EXPECT_THAT(payloads, ElementsAre("hello", "world"));
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
