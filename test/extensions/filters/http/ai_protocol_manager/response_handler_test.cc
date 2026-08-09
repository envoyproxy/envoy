#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/response_handler.h"

#include "test/common/stats/stat_test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Test-only access to the retained buffer, for asserting the hard memory cap.
// Retention is gated before every copy, so the invariant is observable at any
// call boundary; scan-work linearity is covered by response_handler_benchmark.
class SseResponseHandlerPeer {
public:
  static uint64_t bufferedBytes(const SseResponseHandler& handler) {
    return handler.buffer_.length();
  }
};

namespace {

// Test caps; production defaults live in filter.cc. The event cap is
// deliberately much smaller than the 1MiB production default so cap behavior
// is exercised with small fixtures.
constexpr uint32_t TestMaxEventSize = 16 * 1024;
constexpr uint32_t TestMaxParsedEvents = 1024;
constexpr uint32_t TestMaxBodySize = 4 * 1024 * 1024;

class ResponseHandlerTest : public testing::Test {
public:
  ResponseHandlerTest()
      : stats_(AiProtocolManagerStats{
            ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*store_.rootScope(), "test."))}) {}

  // Feed `body` to the handler in frames of `frame_size` bytes, then signal
  // end-of-stream. Exercises reassembly across arbitrary boundaries.
  void feed(ResponseHandler& handler, absl::string_view body, size_t frame_size,
            bool end_stream = true) {
    for (size_t offset = 0; offset < body.size(); offset += frame_size) {
      Buffer::OwnedImpl frame(body.substr(offset, frame_size));
      handler.onData(frame);
    }
    if (end_stream) {
      handler.onEndStream();
    }
  }

  Stats::TestUtil::TestStore store_;
  AiProtocolManagerStats stats_;
};

// ---------------------------------------------------------------------------
// SSE handler.

const char OpenAiChatStream[] =
    "data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\","
    "\"choices\":[{\"delta\":{\"content\":\"Hel\"}}],\"usage\":null}\n\n"
    "data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\","
    "\"choices\":[{\"delta\":{\"content\":\"lo\"}}],\"usage\":null}\n\n"
    "data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\","
    "\"choices\":[],\"usage\":{\"prompt_tokens\":19,"
    "\"completion_tokens\":10,\"total_tokens\":29}}\n\n"
    "data: [DONE]\n\n";

// The stream parses identically regardless of how it is sliced into frames.
TEST_F(ResponseHandlerTest, SseOpenAiAcrossArbitraryFrameBoundaries) {
  for (const size_t frame_size : {1, 7, 64, 4096}) {
    SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
    feed(handler, OpenAiChatStream, frame_size);
    EXPECT_EQ(handler.usage().input_tokens, 19) << "frame_size=" << frame_size;
    EXPECT_EQ(handler.usage().output_tokens, 10) << "frame_size=" << frame_size;
    EXPECT_EQ(handler.usage().total_tokens, 29) << "frame_size=" << frame_size;
    EXPECT_EQ(handler.usage().api_format, ApiFormat::OpenAi);
    EXPECT_EQ(handler.usage().model, "gpt-4o");
    EXPECT_TRUE(handler.parsingComplete()); // [DONE] seen.
  }
}

TEST_F(ResponseHandlerTest, SseOpenAiWithoutIncludeUsageYieldsNothing) {
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(
      handler,
      "data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"delta\":{}}],\"usage\":null}\n\n"
      "data: [DONE]\n\n",
      17);
  EXPECT_FALSE(handler.usage().hasAny());
  EXPECT_TRUE(handler.parsingComplete());
  EXPECT_EQ(stats_.response_parse_error_.value(), 0);
}

TEST_F(ResponseHandlerTest, SseAnthropicNamedEventsCumulative) {
  const std::string stream =
      "event: message_start\r\n"
      "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"type\":\"message\","
      "\"model\":\"claude-opus-5\",\"content\":[],\"usage\":{\"input_tokens\":2679,"
      "\"cache_read_input_tokens\":128,\"output_tokens\":3}}}\r\n\r\n"
      "event: ping\r\n"
      "data: {\"type\":\"ping\"}\r\n\r\n"
      "event: content_block_delta\r\n"
      "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\","
      "\"text\":\"Hi\"}}\r\n\r\n"
      "event: message_delta\r\n"
      "data: {\"type\":\"message_delta\",\"delta\":{},\"usage\":{\"output_tokens\":7}}\r\n\r\n"
      "event: message_delta\r\n"
      "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
      "\"usage\":{\"output_tokens\":15}}\r\n\r\n"
      "event: message_stop\r\n"
      "data: {\"type\":\"message_stop\"}\r\n\r\n";
  for (const size_t frame_size : {3, 50, 8192}) {
    SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
    feed(handler, stream, frame_size);
    EXPECT_EQ(handler.usage().input_tokens, 2679) << "frame_size=" << frame_size; // Native.
    EXPECT_EQ(handler.usage().output_tokens, 15) << "frame_size=" << frame_size;
    EXPECT_EQ(handler.usage().cached_input_tokens, 128);
    EXPECT_EQ(handler.usage().model, "claude-opus-5");
    EXPECT_EQ(handler.usage().api_format, ApiFormat::Anthropic);
    EXPECT_TRUE(handler.parsingComplete()); // message_stop seen.
    EXPECT_FALSE(handler.degraded());
    TokenUsage finalized = handler.usage();
    finalized.finalize();
    // Canonical inclusive input: 2679 uncached + 128 cached reads.
    EXPECT_EQ(finalized.input_tokens, 2807);
    EXPECT_EQ(finalized.total_tokens, 2822); // 2807 + 15, inclusive.
  }
}

TEST_F(ResponseHandlerTest, PingDoesNotPoisonAutoDetection) {
  // A bare JSON heartbeat is a weak marker any gateway may emit: it must not
  // lock the stream format, or a subsequent OpenAI stream would be misparsed.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler,
       "data: {\"type\":\"ping\"}\n\n"
       "data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
       "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5,\"total_tokens\":15}}\n\n"
       "data: [DONE]\n\n",
       4096);
  EXPECT_EQ(handler.usage().api_format, ApiFormat::OpenAi);
  EXPECT_EQ(handler.usage().total_tokens, 15);
}

TEST_F(ResponseHandlerTest, SseGeminiAltSseLastSnapshotWins) {
  const std::string stream =
      "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"a\"}]}}],"
      "\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":16,"
      "\"totalTokenCount\":22}}\n\n"
      "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"b\"}]},"
      "\"finishReason\":\"STOP\"}],\"usageMetadata\":{\"promptTokenCount\":6,"
      "\"candidatesTokenCount\":149,\"totalTokenCount\":155},"
      "\"modelVersion\":\"gemini-2.5-flash\"}\n\n";
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler, stream, 11);
  EXPECT_EQ(handler.usage().output_tokens, 149);
  EXPECT_EQ(handler.usage().total_tokens, 155);
  EXPECT_EQ(handler.usage().model, "gemini-2.5-flash");
  // No terminator in this dialect; extraction finalizes at end of stream.
  EXPECT_FALSE(handler.parsingComplete());
}

TEST_F(ResponseHandlerTest, SseCommentKeepAlivesAndUnknownEventsSkipped) {
  SseResponseHandler handler(ApiFormat::Anthropic, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler,
       ": keep-alive\n\n"
       "event: some_future_event\ndata: {\"type\":\"some_future_event\"}\n\n"
       "data: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":4,\"input_tokens\":2}}\n\n",
       4096);
  EXPECT_EQ(handler.usage().output_tokens, 4);
  EXPECT_EQ(stats_.response_parse_error_.value(), 0);
}

TEST_F(ResponseHandlerTest, SseInvalidJsonEventCountedAndSkipped) {
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler,
       "data: this-is-not-json\n\n"
       "data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
       "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,\"total_tokens\":3}}\n\n",
       4096);
  EXPECT_EQ(stats_.response_parse_error_.value(), 1);
  EXPECT_EQ(handler.usage().total_tokens, 3); // Later events still processed.
}

TEST_F(ResponseHandlerTest, SseOversizedPendingEventDiscarded) {
  SseResponseHandler handler(ApiFormat::Unknown, /*max_event_size=*/64, TestMaxParsedEvents,
                             stats_);
  // More than 64 bytes with no blank-line delimiter anywhere.
  const std::string unterminated = "data: {\"pad\":\"" + std::string(200, 'x') + "\"";
  feed(handler, unterminated, 32, /*end_stream=*/false);
  // Exactly one increment per oversized event: once discarding, further frames
  // of the same event are dropped without re-charging the counter.
  EXPECT_EQ(stats_.sse_event_too_large_.value(), 1);
  EXPECT_FALSE(handler.usage().hasAny());
  EXPECT_TRUE(handler.degraded());
}

TEST_F(ResponseHandlerTest, SseExplicitFormatSkipsDetection) {
  // Without discriminating markers, AUTO would skip this; a configured
  // format extracts it.
  SseResponseHandler handler(ApiFormat::Anthropic, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler, "data: {\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}\n\n", 4096);
  EXPECT_EQ(handler.usage().input_tokens, 5);
  EXPECT_EQ(handler.usage().output_tokens, 7);
}

TEST_F(ResponseHandlerTest, SseOversizedCompleteEventSkippedNotParsed) {
  // A fully delimited event over the cap must not bypass the limit: it is
  // skipped (no parse, no usage), and later small events are still processed.
  SseResponseHandler handler(ApiFormat::Unknown, /*max_event_size=*/256, TestMaxParsedEvents,
                             stats_);
  const std::string oversized = "data: {\"object\":\"chat.completion.chunk\",\"pad\":\"" +
                                std::string(4096, 'x') +
                                "\",\"usage\":{\"prompt_tokens\":999,\"completion_tokens\":999,"
                                "\"total_tokens\":1998}}\n\n";
  const std::string small = "data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
                            "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,"
                            "\"total_tokens\":3}}\n\n";
  // Deliver in one frame (oversized event complete on arrival) and fragmented.
  for (const size_t frame_size : {size_t(1 << 20), size_t(16)}) {
    SseResponseHandler fresh(ApiFormat::Unknown, /*max_event_size=*/64, TestMaxParsedEvents,
                             stats_);
    feed(fresh, oversized, frame_size, /*end_stream=*/false);
    EXPECT_FALSE(fresh.usage().hasAny()) << "frame_size=" << frame_size;
  }
  EXPECT_GE(stats_.sse_event_too_large_.value(), 2);
  // After skipping an oversized event delivered whole, extraction continues.
  feed(handler, oversized + small, 1 << 20);
  EXPECT_EQ(handler.usage().total_tokens, 3);
}

TEST_F(ResponseHandlerTest, SseTailAfterDoneSentinelIgnored) {
  // Junk after [DONE] in the same frame must not produce parse errors or
  // retained extraction work.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler,
       "data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
       "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,\"total_tokens\":3}}\n\n"
       "data: [DONE]\n\ndata: junk-after-done\n\n",
       1 << 20, /*end_stream=*/false);
  Buffer::OwnedImpl later("data: more-junk\n\n");
  handler.onData(later);
  handler.onEndStream();
  EXPECT_TRUE(handler.parsingComplete());
  EXPECT_EQ(handler.usage().total_tokens, 3);
  EXPECT_EQ(stats_.response_parse_error_.value(), 0);
}

TEST_F(ResponseHandlerTest, SseDiscardSplitTerminatorDoesNotFabricateBoundary) {
  // The oversized event's later `data:` line is separated from the discarded
  // prefix only by a line terminator arriving at a frame edge. The scanner's
  // line state must remember that the terminator ended a *content* line, so no
  // event boundary is fabricated and the suffix (a well-formed usage chunk
  // small enough to parse) is not extracted.
  SseResponseHandler handler(ApiFormat::Unknown, /*max_event_size=*/256, TestMaxParsedEvents,
                             stats_);
  Buffer::OwnedImpl frame1("data: " + std::string(300, 'x'));
  handler.onData(frame1);
  Buffer::OwnedImpl frame2("\nda");
  handler.onData(frame2);
  Buffer::OwnedImpl frame3("ta: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
                           "\"usage\":{\"prompt_tokens\":9,\"completion_tokens\":9,"
                           "\"total_tokens\":999}}\n\n");
  handler.onData(frame3);
  handler.onEndStream();
  EXPECT_FALSE(handler.usage().hasAny());
  EXPECT_EQ(stats_.sse_event_too_large_.value(), 1);

  // Control: a real blank line at the same frame position ends the oversized
  // event, so the following line IS a fresh event and extracts.
  SseResponseHandler control(ApiFormat::Unknown, /*max_event_size=*/256, TestMaxParsedEvents,
                             stats_);
  Buffer::OwnedImpl c1("data: " + std::string(300, 'x'));
  control.onData(c1);
  Buffer::OwnedImpl c2("\n\nda");
  control.onData(c2);
  Buffer::OwnedImpl c3("ta: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
                       "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,"
                       "\"total_tokens\":3}}\n\n");
  control.onData(c3);
  control.onEndStream();
  EXPECT_EQ(control.usage().total_tokens, 3);
}

TEST_F(ResponseHandlerTest, SseExtractionIdenticalUnderExtremeFragmentation) {
  // 1-byte frames through a multi-event stream must produce the identical
  // result as whole-stream delivery (the scanner is a pure byte state
  // machine); scan-work linearity itself is covered by
  // response_handler_benchmark.
  const std::string stream =
      "event: message_start\r\n"
      "data: {\"type\":\"message_start\",\"message\":{\"model\":\"claude-opus-5\","
      "\"usage\":{\"input_tokens\":9,\"cache_read_input_tokens\":4,\"output_tokens\":1}}}"
      "\r\n\r\n"
      "data: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":6}}\n\n"
      "data: {\"type\":\"message_stop\"}\n\n";
  SseResponseHandler byte_wise(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(byte_wise, stream, 1);
  SseResponseHandler whole(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(whole, stream, stream.size());
  EXPECT_EQ(byte_wise.usage().input_tokens, whole.usage().input_tokens);
  EXPECT_EQ(byte_wise.usage().output_tokens, 6);
  EXPECT_EQ(whole.usage().output_tokens, 6);
  EXPECT_EQ(byte_wise.parsingComplete(), whole.parsingComplete());
}

TEST_F(ResponseHandlerTest, SseSingleFrameOversizedEventIsStrictlyBounded) {
  // max_event_size is a hard bound on retained extraction state, not a
  // best-effort one: a single multi-megabyte data callback (a previous encode
  // filter can legally deliver one, e.g. after buffering or decompression)
  // must not be copied into the side buffer before the cap check runs.
  constexpr uint32_t cap = 1024;
  const std::string huge_interior = "data: {\"pad\":\"" + std::string(8 * 1024 * 1024, 'x');
  {
    // One frame containing an unterminated oversized event. Retention is
    // gated before every copy, so the buffer respects the cap at every call
    // boundary (there is no intra-call spike to observe).
    SseResponseHandler handler(ApiFormat::Unknown, cap, TestMaxParsedEvents, stats_);
    Buffer::OwnedImpl frame(huge_interior);
    handler.onData(frame);
    EXPECT_LE(SseResponseHandlerPeer::bufferedBytes(handler), cap);
    EXPECT_EQ(stats_.sse_event_too_large_.value(), 1);
    EXPECT_TRUE(handler.degraded());
  }
  {
    // One frame containing a complete oversized event (terminator at the
    // end), followed by a small event that must still extract.
    SseResponseHandler handler(ApiFormat::Unknown, cap, TestMaxParsedEvents, stats_);
    Buffer::OwnedImpl frame(huge_interior + "\"}\n\n"
                                            "data: {\"object\":\"chat.completion.chunk\","
                                            "\"choices\":[],\"usage\":{\"prompt_tokens\":1,"
                                            "\"completion_tokens\":2,\"total_tokens\":3}}\n\n");
    handler.onData(frame);
    EXPECT_LE(SseResponseHandlerPeer::bufferedBytes(handler), cap);
    handler.onEndStream();
    EXPECT_EQ(handler.usage().total_tokens, 3);
    EXPECT_TRUE(handler.degraded());
  }
  {
    // Byte-at-a-time delivery of an over-cap event: the invariant holds at
    // every one of the ~cap call boundaries around the discard transition.
    SseResponseHandler handler(ApiFormat::Unknown, cap, TestMaxParsedEvents, stats_);
    const std::string prefix = huge_interior.substr(0, cap + 64);
    for (const char c : prefix) {
      Buffer::OwnedImpl byte(absl::string_view(&c, 1));
      handler.onData(byte);
      ASSERT_LE(SseResponseHandlerPeer::bufferedBytes(handler), cap);
    }
    EXPECT_TRUE(handler.degraded());
  }
}

TEST_F(ResponseHandlerTest, WeakGeminiMarkerDoesNotPoisonAutoDetection) {
  // A document whose `candidates` value is not an array (generic key, foreign
  // dialect) must not lock the stream to Gemini; the later OpenAI usage chunk
  // must still be detected and extracted.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler,
       "data: {\"candidates\":\"not-gemini\"}\n\n"
       "data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
       "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5,\"total_tokens\":15}}\n\n"
       "data: [DONE]\n\n",
       4096);
  EXPECT_EQ(handler.usage().api_format, ApiFormat::OpenAi);
  EXPECT_EQ(handler.usage().total_tokens, 15);
}

TEST_F(ResponseHandlerTest, GeminiToolUseBreakdownSurvivesHandlerMerge) {
  // Reviewer merge-gate: the tool-use breakdown must survive the handler's
  // merge path (a direct extract() call would bypass it) all the way to the
  // finalized result a consumer sees.
  const std::string body =
      R"({"usageMetadata":{"promptTokenCount":6,"toolUsePromptTokenCount":5,
          "candidatesTokenCount":149,"thoughtsTokenCount":12,"totalTokenCount":172}})";
  JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_);
  feed(handler, body, 4096);
  EXPECT_EQ(handler.usage().tool_use_input_tokens, 5);
  TokenUsage finalized = handler.usage();
  finalized.finalize();
  EXPECT_EQ(finalized.input_tokens, 11);
  EXPECT_EQ(finalized.output_tokens, 161);
  EXPECT_EQ(finalized.total_tokens, 172);
  EXPECT_EQ(finalized.tool_use_input_tokens, 5);
}

TEST_F(ResponseHandlerTest, JsonArrayStopsAtTerminalEvent) {
  // Terminal semantics are transport-independent: a terminal event inside a
  // streamed JSON array must not be overridden by later elements, matching
  // the SSE path's early stop.
  const std::string body =
      R"([{"type":"response.completed","response":{"object":"response",
           "usage":{"input_tokens":1,"output_tokens":2,"total_tokens":3}}},
          {"response":{"usage":{"input_tokens":100,"output_tokens":200,
           "total_tokens":999}}}])";
  JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_);
  feed(handler, body, 4096);
  EXPECT_EQ(handler.usage().total_tokens, 3);
}

TEST_F(ResponseHandlerTest, SseOversizedEventSuffixNotMisparsed) {
  // A later `data:` line of the SAME oversized event (no blank-line boundary
  // in between) must not be misread as a fresh event after the pending data
  // was discarded. The whole event is skipped; the next real event parses.
  SseResponseHandler handler(ApiFormat::Unknown, /*max_event_size=*/64, TestMaxParsedEvents,
                             stats_);
  Buffer::OwnedImpl frame1("data: " + std::string(70, 'x')); // Unterminated, over cap.
  handler.onData(frame1);
  Buffer::OwnedImpl frame2("\ndata: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
                           "\"usage\":{\"prompt_tokens\":9,\"completion_tokens\":9,"
                           "\"total_tokens\":999}}\n\n");
  handler.onData(frame2);
  handler.onEndStream();
  EXPECT_FALSE(handler.usage().hasAny()); // The suffix belongs to the skipped event.
  EXPECT_EQ(stats_.sse_event_too_large_.value(), 1);
}

TEST_F(ResponseHandlerTest, SseDiscardedEventResumesAtNextRealEvent) {
  SseResponseHandler handler(ApiFormat::Unknown, /*max_event_size=*/256, TestMaxParsedEvents,
                             stats_);
  // Oversized event fragmented across frames, terminated mid-frame, followed
  // by a genuine small event.
  Buffer::OwnedImpl frame1("data: " + std::string(400, 'x'));
  handler.onData(frame1);
  Buffer::OwnedImpl frame2("y\n\n" // Terminates the oversized event.
                           "data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
                           "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,"
                           "\"total_tokens\":3}}\n\n");
  handler.onData(frame2);
  handler.onEndStream();
  EXPECT_EQ(handler.usage().total_tokens, 3);
  EXPECT_EQ(stats_.sse_event_too_large_.value(), 1); // Counted once per oversized event.
}

TEST_F(ResponseHandlerTest, SseDiscardBoundaryStraddlesFrames) {
  // The oversized event's terminating blank line arrives split across frames
  // (CRLF CRLF delivered one byte at a time): the retained tail must still
  // detect the boundary, and the following event must parse.
  SseResponseHandler handler(ApiFormat::Unknown, /*max_event_size=*/256, TestMaxParsedEvents,
                             stats_);
  Buffer::OwnedImpl big("data: " + std::string(400, 'x'));
  handler.onData(big);
  for (const char c : {'\r', '\n', '\r', '\n'}) {
    Buffer::OwnedImpl byte(absl::string_view(&c, 1));
    handler.onData(byte);
  }
  Buffer::OwnedImpl next("data: {\"type\":\"message_delta\",\"usage\":{\"input_tokens\":5,"
                         "\"output_tokens\":7}}\n\n");
  handler.onData(next);
  handler.onEndStream();
  EXPECT_EQ(handler.usage().input_tokens, 5);
  EXPECT_EQ(handler.usage().output_tokens, 7);
}

TEST_F(ResponseHandlerTest, NonObjectJsonShapesTolerated) {
  // Valid JSON whose root or elements are not objects must be skipped without
  // reaching object-only accessors (an IS_ENVOY_BUG path in the JSON impl).
  for (const std::string& body :
       {std::string("[1]"),
        std::string("[null,{\"usageMetadata\":{\"promptTokenCount\":1,"
                    "\"candidatesTokenCount\":2,\"totalTokenCount\":3}}]"),
        std::string("[[1],{\"usageMetadata\":{\"promptTokenCount\":1,"
                    "\"candidatesTokenCount\":2,\"totalTokenCount\":3}}]"),
        std::string("\"just-a-string\"")}) {
    JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_);
    feed(handler, body, 4096);
    if (absl::StrContains(body, "usageMetadata")) {
      // Object elements alongside non-object ones still extract.
      EXPECT_EQ(handler.usage().total_tokens, 3) << body;
    } else {
      EXPECT_FALSE(handler.usage().hasAny()) << body;
    }
  }
  // SSE events whose data payload is a JSON array or scalar.
  SseResponseHandler sse(ApiFormat::Gemini, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(sse, "data: []\n\ndata: 42\n\n", 4096);
  EXPECT_FALSE(sse.usage().hasAny());
  // Scalar roots come back from the loader as a null root and are counted as
  // parse errors (the string body above and the `42` payload); array roots
  // are skipped silently. Either way, nothing crashes and nothing extracts.
  EXPECT_EQ(stats_.response_parse_error_.value(), 2);
}

TEST_F(ResponseHandlerTest, SseSplitCarriageReturnResolvedAtEndOfStream) {
  // An event whose final line terminator is a bare CR pending at EOF only
  // resolves when the parser sees end-of-stream (delivered via onEndStream).
  SseResponseHandler handler(ApiFormat::Anthropic, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler, "data: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":9}}\r\n\r",
       /*frame_size=*/4096, /*end_stream=*/false);
  EXPECT_FALSE(handler.usage().hasAny()); // Still pending without EOS.
  handler.onEndStream();
  EXPECT_EQ(handler.usage().output_tokens, 9);
}

TEST_F(ResponseHandlerTest, IncompleteTerminalSseEventMarksPartial) {
  // The stream ends without the final event's terminating blank line: the SSE
  // spec discards the incomplete event, but the discarded input may have been
  // the usage-bearing final snapshot, so the surviving earlier counts must be
  // flagged.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler,
       "data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":16,"
       "\"totalTokenCount\":22}}\n\n"
       "data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":149,"
       "\"totalTokenCount\":155}}",
       4096); // No trailing blank line; feed() signals end of stream.
  EXPECT_EQ(handler.usage().total_tokens, 22); // The stale snapshot survives...
  EXPECT_TRUE(handler.degraded());             // ...but is flagged partial.
  EXPECT_EQ(stats_.sse_incomplete_event_.value(), 1);

  // A cleanly terminated stream does not trip the flag.
  SseResponseHandler clean(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(clean,
       "data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":16,"
       "\"totalTokenCount\":22}}\n\n",
       4096);
  EXPECT_FALSE(clean.degraded());
  EXPECT_EQ(stats_.sse_incomplete_event_.value(), 1);
}

TEST_F(ResponseHandlerTest, MalformedFinalUsageFieldsMarkPartial) {
  // The final cumulative snapshot parses as JSON but its known count fields
  // are unusable: the earlier snapshot's counts survive, flagged partial.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler,
       "data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":16,"
       "\"totalTokenCount\":22}}\n\n"
       "data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":\"149\","
       "\"totalTokenCount\":\"155\"}}\n\n",
       4096);
  EXPECT_EQ(handler.usage().output_tokens, 16);
  EXPECT_EQ(handler.usage().total_tokens, 22);
  EXPECT_TRUE(handler.degraded());
  EXPECT_EQ(stats_.malformed_usage_field_.value(), 1);
}

TEST_F(ResponseHandlerTest, GenericMessageEventDoesNotPoisonAutoDetection) {
  // A structureless `{"type":"message"}` from a non-Anthropic gateway must not
  // lock the stream; the later OpenAI usage chunk still detects and extracts.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_);
  feed(handler,
       "data: {\"type\":\"message\"}\n\n"
       "data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
       "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5,\"total_tokens\":15}}\n\n"
       "data: [DONE]\n\n",
       4096);
  EXPECT_EQ(handler.usage().api_format, ApiFormat::OpenAi);
  EXPECT_EQ(handler.usage().total_tokens, 15);
}

// Minimal recording account: verifies the handlers charge retained bytes to
// the stream's buffer memory account and credit them back on release.
class TestAccount : public Buffer::BufferMemoryAccount {
public:
  void charge(uint64_t amount) override {
    balance_ += amount;
    peak_ = std::max(peak_, balance_);
  }
  void credit(uint64_t amount) override {
    ASSERT(amount <= balance_);
    balance_ -= amount;
  }
  void clearDownstream() override {}
  void resetDownstream() override {}
  uint64_t balance() const { return balance_; }
  uint64_t peak() const { return peak_; }

private:
  uint64_t balance_{0};
  uint64_t peak_{0};
};

TEST_F(ResponseHandlerTest, ObservationBuffersUseStreamMemoryAccount) {
  auto account = std::make_shared<TestAccount>();
  {
    SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_,
                               account);
    Buffer::OwnedImpl partial("data: {\"object\":\"chat.completion.chunk\"");
    handler.onData(partial);
    EXPECT_GT(account->balance(), 0); // Retained bytes are charged.
    handler.onEndStream();
    EXPECT_EQ(account->balance(), 0); // Released state is credited back.
  }
  EXPECT_EQ(account->balance(), 0);
  {
    JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_, account);
    Buffer::OwnedImpl body("{\"type\":\"message\",\"usage\":{\"input_tokens\":5}}");
    handler.onData(body);
    EXPECT_GT(account->balance(), 0);
    handler.onEndStream();
    EXPECT_EQ(account->balance(), 0);
  }
  EXPECT_EQ(account->balance(), 0);
}

TEST_F(ResponseHandlerTest, ContiguousEventsNeverChargeTheAccount) {
  // The zero-copy fast path: a complete event arriving within one frame is
  // processed in place. Nothing is retained, so the account is never charged
  // -- the dominant shape of real streams costs no side memory at all.
  auto account = std::make_shared<TestAccount>();
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, TestMaxParsedEvents, stats_,
                             account);
  feed(handler, OpenAiChatStream, /*frame_size=*/1 << 20);
  EXPECT_EQ(handler.usage().total_tokens, 29);
  EXPECT_EQ(account->peak(), 0);
}

TEST_F(ResponseHandlerTest, FragmentedNearCapEventPeakChargeIsBounded) {
  // A split event goes through the retained buffer and its consolidating
  // linearize(), which can transiently hold a second copy: the peak account
  // charge is bounded by twice the cap (and only for split events).
  constexpr uint32_t cap = 8 * 1024;
  auto account = std::make_shared<TestAccount>();
  SseResponseHandler handler(ApiFormat::Unknown, cap, TestMaxParsedEvents, stats_, account);
  const std::string stream = "data: {\"object\":\"chat.completion.chunk\",\"pad\":\"" +
                             std::string(6 * 1024, 'x') +
                             "\",\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,"
                             "\"total_tokens\":3}}\n\n";
  feed(handler, stream, /*frame_size=*/512);
  EXPECT_EQ(handler.usage().total_tokens, 3);
  EXPECT_GT(account->peak(), 0);
  EXPECT_LE(account->peak(), 2 * uint64_t(cap));
  EXPECT_EQ(account->balance(), 0);
}

TEST_F(ResponseHandlerTest, ParseBudgetExhaustionGoesInertAndPartial) {
  // The stream-wide budget bounds how many payloads are JSON-parsed. Once
  // exhausted the handler goes inert: accumulated usage survives (published
  // partial by the filter), later events cost nothing, and one counter fires.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, /*max_parsed_events=*/3, stats_);
  Buffer::OwnedImpl first("data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
                          "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,"
                          "\"total_tokens\":3}}\n\n");
  handler.onData(first);
  for (int i = 0; i < 10; ++i) {
    Buffer::OwnedImpl flood("data: {\"x\":1}\n\n");
    handler.onData(flood);
  }
  handler.onEndStream();
  EXPECT_EQ(handler.usage().total_tokens, 3); // Survives exhaustion.
  EXPECT_TRUE(handler.degraded());
  EXPECT_EQ(stats_.sse_event_budget_exhausted_.value(), 1); // Counted once.
}

TEST_F(ResponseHandlerTest, MalformedEventFloodIsBudgeted) {
  // Malformed payloads consume budget too: a stream repeating `data: x` can
  // only charge the parse-error counter a bounded number of times.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, /*max_parsed_events=*/5, stats_);
  for (int i = 0; i < 50; ++i) {
    Buffer::OwnedImpl flood("data: not-json\n\n");
    handler.onData(flood);
  }
  handler.onEndStream();
  EXPECT_EQ(stats_.response_parse_error_.value(), 5); // Bounded by the budget.
  EXPECT_EQ(stats_.sse_event_budget_exhausted_.value(), 1);
  EXPECT_TRUE(handler.degraded());
}

TEST_F(ResponseHandlerTest, SkippableNamedEventsAreNeitherParsedNorBudgeted) {
  // Named events that cannot carry usage are dropped on their `event:` line
  // alone, before the data payload is assembled or parsed: deliberately
  // invalid JSON payloads produce no parse errors, and a delta flood does not
  // consume the parse budget that the terminal usage event needs.
  SseResponseHandler handler(ApiFormat::Unknown, TestMaxEventSize, /*max_parsed_events=*/2, stats_);
  for (int i = 0; i < 20; ++i) {
    Buffer::OwnedImpl anthropic_delta("event: content_block_delta\n"
                                      "data: this{is)not[json\n\n");
    handler.onData(anthropic_delta);
    Buffer::OwnedImpl responses_delta("event: response.output_text.delta\n"
                                      "data: also{not)json\n\n");
    handler.onData(responses_delta);
  }
  Buffer::OwnedImpl terminal(
      "event: response.completed\n"
      "data: {\"type\":\"response.completed\",\"response\":{\"object\":\"response\","
      "\"usage\":{\"input_tokens\":3,\"output_tokens\":4,\"total_tokens\":7}}}\n\n");
  handler.onData(terminal);
  handler.onEndStream();
  EXPECT_EQ(stats_.response_parse_error_.value(), 0); // Deltas never parsed.
  EXPECT_EQ(handler.usage().total_tokens, 7);         // Budget was preserved.
  EXPECT_TRUE(handler.parsingComplete());
  EXPECT_FALSE(handler.degraded());
}

// ---------------------------------------------------------------------------
// JSON handler.

TEST_F(ResponseHandlerTest, JsonBodyAcrossFrames) {
  const std::string body =
      R"({"id":"msg_1","type":"message","model":"claude-opus-5",
          "usage":{"input_tokens":2095,"output_tokens":503}})";
  for (const size_t frame_size : {1, 13, 4096}) {
    JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_);
    feed(handler, body, frame_size);
    EXPECT_EQ(handler.usage().input_tokens, 2095) << "frame_size=" << frame_size;
    EXPECT_EQ(handler.usage().output_tokens, 503) << "frame_size=" << frame_size;
    EXPECT_EQ(handler.usage().api_format, ApiFormat::Anthropic);
  }
}

TEST_F(ResponseHandlerTest, JsonRootArrayIsGeminiDefaultStreaming) {
  // Gemini :streamGenerateContent without ?alt=sse: the complete body is a JSON
  // array of chunks; the last snapshot is authoritative.
  const std::string body =
      R"([{"candidates":[{"content":{"parts":[{"text":"a"}]}}],
           "usageMetadata":{"promptTokenCount":6,"candidatesTokenCount":16,"totalTokenCount":22}},
          {"candidates":[{"content":{"parts":[{"text":"b"}]},"finishReason":"STOP"}],
           "usageMetadata":{"promptTokenCount":6,"candidatesTokenCount":149,"totalTokenCount":155},
           "modelVersion":"gemini-2.5-flash"}])";
  JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_);
  feed(handler, body, 37);
  EXPECT_EQ(handler.usage().input_tokens, 6);
  EXPECT_EQ(handler.usage().output_tokens, 149);
  EXPECT_EQ(handler.usage().total_tokens, 155);
  EXPECT_EQ(handler.usage().api_format, ApiFormat::Gemini);
}

TEST_F(ResponseHandlerTest, JsonBodyOverLimitSkipsExtraction) {
  JsonResponseHandler handler(ApiFormat::Unknown, /*max_inspected_body_size=*/32, stats_);
  const std::string body =
      R"({"usage":{"prompt_tokens":1},"pad":")" + std::string(100, 'x') + R"("})";
  feed(handler, body, 16);
  EXPECT_EQ(stats_.response_body_too_large_.value(), 1);
  EXPECT_FALSE(handler.usage().hasAny());
}

TEST_F(ResponseHandlerTest, JsonInvalidBodyCounted) {
  JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_);
  feed(handler, "<html>bad gateway</html>", 4096);
  EXPECT_EQ(stats_.response_parse_error_.value(), 1);
  EXPECT_FALSE(handler.usage().hasAny());
}

TEST_F(ResponseHandlerTest, JsonNonObjectRootTolerated) {
  JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_);
  feed(handler, "42", 4096);
  EXPECT_FALSE(handler.usage().hasAny());
}

TEST_F(ResponseHandlerTest, JsonUnknownShapeYieldsNothing) {
  JsonResponseHandler handler(ApiFormat::Unknown, TestMaxBodySize, stats_);
  feed(handler, R"({"status":"ok"})", 4096);
  EXPECT_FALSE(handler.usage().hasAny());
  EXPECT_EQ(stats_.response_parse_error_.value(), 0);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
