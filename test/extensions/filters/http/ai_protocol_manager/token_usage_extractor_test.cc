#include "source/extensions/filters/http/ai_protocol_manager/token_usage_extractor.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

nlohmann::json parse(const std::string& json) {
  nlohmann::json result = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  EXPECT_FALSE(result.is_discarded()) << json;
  return result;
}

// Most cases only need the usage half of the extraction result; malformed-flag
// behavior is covered by the dedicated tests below.
TokenUsage extractUsage(ApiProtocol format, const nlohmann::json& json) {
  return TokenUsageExtractor::extract(format, json).usage;
}

// ---------------------------------------------------------------------------
// Provider detection.

TEST(DetectFormatTest, OpenAiChatCompletion) {
  EXPECT_EQ(
      TokenUsageExtractor::detectFormat(parse(R"({"object":"chat.completion","model":"gpt-4o"})")),
      ApiProtocol::OpenAiChatCompletions);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(
                parse(R"({"object":"chat.completion.chunk","choices":[]})")),
            ApiProtocol::OpenAiChatCompletions);
}

TEST(DetectFormatTest, OpenAiResponsesApi) {
  // Non-streaming Response object and streaming lifecycle events.
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"object":"response","output":[]})")),
            ApiProtocol::OpenAiResponses);
  EXPECT_EQ(
      TokenUsageExtractor::detectFormat(parse(R"({"type":"response.completed","response":{}})")),
      ApiProtocol::OpenAiResponses);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(
                parse(R"({"type":"response.output_text.delta","delta":"hi"})")),
            ApiProtocol::OpenAiResponses);
}

TEST(DetectFormatTest, Anthropic) {
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"type":"message","role":"assistant"})")),
            ApiProtocol::AnthropicMessages);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"type":"message_start","message":{}})")),
            ApiProtocol::AnthropicMessages);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(
                parse(R"({"type":"message_delta","usage":{"output_tokens":3}})")),
            ApiProtocol::AnthropicMessages);
  EXPECT_EQ(
      TokenUsageExtractor::detectFormat(parse(R"({"type":"message","usage":{"input_tokens":1}})")),
      ApiProtocol::AnthropicMessages);
}

TEST(DetectFormatTest, StructurelessAnthropicShapedTypesDoNotLock) {
  // A bare `type` string with no corroborating structure is generic: any
  // gateway can emit it, and none of these can carry usage, so treating them
  // as evidence risks poisoning a foreign stream (a lone message_stop would
  // even terminate extraction). In a genuine Anthropic stream, message_start
  // or the non-streaming Message locks the format first.
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"type":"message"})")),
            ApiProtocol::Unspecified);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"type":"message_stop"})")),
            ApiProtocol::Unspecified);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"type":"content_block_delta"})")),
            ApiProtocol::Unspecified);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"type":"message_start"})")),
            ApiProtocol::Unspecified);
}

TEST(DetectFormatTest, Gemini) {
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"candidates":[{"content":{}}]})")),
            ApiProtocol::GeminiGenerateContent);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"usageMetadata":{}})")),
            ApiProtocol::GeminiGenerateContent);
}

TEST(DetectFormatTest, Unknown) {
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"foo":"bar"})")), ApiProtocol::Unspecified);
  // A bare content delta with no discriminating markers.
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"text":"hello"})")),
            ApiProtocol::Unspecified);
  // `ping` is a weak marker any gateway may emit: it must not lock detection.
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"type":"ping"})")),
            ApiProtocol::Unspecified);
}

TEST(DetectFormatTest, TypeMismatchedMarkersDoNotLock) {
  // Marker keys are generic names; only the Gemini value *shape* counts.
  // hasObject()-style key presence alone must not lock the stream: detection
  // is stream-global, and a foreign document with a `candidates` string would
  // otherwise poison every later event.
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"candidates":"not-gemini"})")),
            ApiProtocol::Unspecified);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"usageMetadata":"nope"})")),
            ApiProtocol::Unspecified);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"usageMetadata":[1]})")),
            ApiProtocol::Unspecified);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"modelVersion":42})")),
            ApiProtocol::Unspecified);
  // An array of non-objects is not a Gemini candidates list, and neither is
  // an empty array: real GenerateContentResponse chunks carry candidate
  // objects, so an empty generic `candidates` must not lock the stream.
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"candidates":[1,"x",null]})")),
            ApiProtocol::Unspecified);
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"candidates":[]})")),
            ApiProtocol::Unspecified);
  // The correctly-shaped markers still detect.
  EXPECT_EQ(TokenUsageExtractor::detectFormat(parse(R"({"modelVersion":"gemini-2.5-pro"})")),
            ApiProtocol::GeminiGenerateContent);
}

// ---------------------------------------------------------------------------
// OpenAI extraction.

TEST(ExtractOpenAiTest, ChatCompletionNonStreaming) {
  const auto usage = extractUsage(ApiProtocol::OpenAiChatCompletions, parse(R"(
      {"id":"chatcmpl-1","object":"chat.completion","model":"gpt-4o-2024-08-06",
       "choices":[{"message":{"content":"hi"}}],
       "usage":{"prompt_tokens":19,"completion_tokens":10,"total_tokens":29,
                "prompt_tokens_details":{"cached_tokens":8,"cache_write_tokens":4},
                "completion_tokens_details":{"reasoning_tokens":3}}})"));
  EXPECT_EQ(usage.input_tokens, 19);
  EXPECT_EQ(usage.output_tokens, 10);
  EXPECT_EQ(usage.total_tokens, 29);
  EXPECT_EQ(usage.cached_input_tokens, 8);
  EXPECT_EQ(usage.cache_creation_input_tokens, 4);
  EXPECT_EQ(usage.reasoning_tokens, 3);
  EXPECT_EQ(usage.model, "gpt-4o-2024-08-06");
  EXPECT_EQ(usage.api_protocol, ApiProtocol::OpenAiChatCompletions);
}

TEST(ExtractOpenAiTest, ChatCompletionUsageChunk) {
  // The include_usage terminal chunk: empty choices, populated usage.
  const auto usage = extractUsage(ApiProtocol::OpenAiChatCompletions, parse(R"(
      {"id":"chatcmpl-1","object":"chat.completion.chunk","model":"gpt-4o-mini",
       "choices":[],"usage":{"prompt_tokens":19,"completion_tokens":10,"total_tokens":29}})"));
  EXPECT_EQ(usage.input_tokens, 19);
  EXPECT_EQ(usage.output_tokens, 10);
  EXPECT_EQ(usage.total_tokens, 29);
}

TEST(ExtractOpenAiTest, UsageNullChunkYieldsNothing) {
  const auto usage = extractUsage(ApiProtocol::OpenAiChatCompletions, parse(R"(
      {"object":"chat.completion.chunk","model":"gpt-4o",
       "choices":[{"delta":{"content":"hi"}}],"usage":null})"));
  EXPECT_FALSE(usage.hasAny());
  EXPECT_EQ(usage.model, "gpt-4o");
}

TEST(ExtractOpenAiTest, ResponsesApiCompletedEvent) {
  const nlohmann::json json = parse(R"(
      {"type":"response.completed","sequence_number":7,
       "response":{"id":"resp_1","object":"response","status":"completed","model":"gpt-5.4",
                   "usage":{"input_tokens":36,
                            "input_tokens_details":{"cached_tokens":12,"cache_write_tokens":6},
                            "output_tokens":87,
                            "output_tokens_details":{"reasoning_tokens":40},
                            "total_tokens":123}}})");
  const auto usage = extractUsage(ApiProtocol::OpenAiResponses, json);
  EXPECT_EQ(usage.input_tokens, 36);
  EXPECT_EQ(usage.output_tokens, 87);
  EXPECT_EQ(usage.total_tokens, 123);
  EXPECT_EQ(usage.cached_input_tokens, 12);
  EXPECT_EQ(usage.cache_creation_input_tokens, 6);
  EXPECT_EQ(usage.reasoning_tokens, 40);
  EXPECT_EQ(usage.model, "gpt-5.4");
  // Terminal lifecycle events end extraction.
  EXPECT_TRUE(TokenUsageExtractor::isTerminalEvent(ApiProtocol::OpenAiResponses, json));
}

TEST(ExtractOpenAiTest, ResponsesApiFailedAndIncompleteAreTerminal) {
  EXPECT_TRUE(TokenUsageExtractor::isTerminalEvent(
      ApiProtocol::OpenAiResponses, parse(R"({"type":"response.failed","response":{}})")));
  EXPECT_TRUE(TokenUsageExtractor::isTerminalEvent(
      ApiProtocol::OpenAiResponses, parse(R"({"type":"response.incomplete","response":{}})")));
  EXPECT_FALSE(TokenUsageExtractor::isTerminalEvent(
      ApiProtocol::OpenAiResponses, parse(R"({"type":"response.in_progress","response":{}})")));
}

// ---------------------------------------------------------------------------
// Anthropic extraction.

TEST(ExtractAnthropicTest, NonStreaming) {
  const auto usage = extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"id":"msg_1","type":"message","role":"assistant","model":"claude-opus-5",
       "usage":{"input_tokens":2095,"output_tokens":503,
                "cache_creation_input_tokens":2051,"cache_read_input_tokens":1024,
                "output_tokens_details":{"thinking_tokens":77}}})"));
  // extract() reports the dialect's native counts (input excludes the two
  // disjoint cache buckets); finalize() computes the canonical inclusive sum.
  EXPECT_EQ(usage.input_tokens, 2095);
  EXPECT_FALSE(usage.total_tokens.has_value());
  TokenUsage finalized = usage;
  finalized.finalize();
  // Canonical inclusive input: 2095 uncached + 2051 cache writes + 1024
  // cache reads.
  EXPECT_EQ(finalized.input_tokens, 5170);
  EXPECT_EQ(finalized.output_tokens, 503);
  // No native total in this dialect; computed from the canonical components.
  EXPECT_EQ(finalized.total_tokens, 5673);
  EXPECT_FALSE(finalized.provider_total_tokens.has_value());
  EXPECT_EQ(finalized.cached_input_tokens, 1024);
  EXPECT_EQ(finalized.cache_creation_input_tokens, 2051);
  EXPECT_EQ(finalized.reasoning_tokens, 77);
  EXPECT_EQ(usage.model, "claude-opus-5");
}

TEST(ExtractAnthropicTest, StreamingAccumulation) {
  TokenUsage accumulated;

  // message_start carries the input side and the model, nested under `message`.
  accumulated.merge(extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"type":"message_start","message":{"id":"msg_1","type":"message","role":"assistant",
       "model":"claude-opus-5","content":[],
       "usage":{"input_tokens":2679,"cache_creation_input_tokens":0,
                "cache_read_input_tokens":0,"output_tokens":3}}})")));
  // Cumulative message_delta events; the last one wins.
  accumulated.merge(
      extractUsage(ApiProtocol::AnthropicMessages,
                   parse(R"({"type":"message_delta","delta":{},"usage":{"output_tokens":7}})")));
  accumulated.merge(extractUsage(
      ApiProtocol::AnthropicMessages,
      parse(
          R"({"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":15}})")));

  accumulated.finalize();
  EXPECT_EQ(accumulated.input_tokens, 2679);
  EXPECT_EQ(accumulated.output_tokens, 15);
  EXPECT_EQ(accumulated.total_tokens, 2694);
  EXPECT_EQ(accumulated.model, "claude-opus-5");
}

TEST(ExtractAnthropicTest, FatMessageDeltaOverridesInputSide) {
  // Newer responses repeat input-side counts in message_delta; last wins.
  TokenUsage accumulated;
  accumulated.merge(extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"type":"message_start","message":{"model":"claude-opus-5",
       "usage":{"input_tokens":100,"output_tokens":1}}})")));
  accumulated.merge(extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"type":"message_delta","delta":{"stop_reason":"end_turn"},
       "usage":{"input_tokens":10682,"cache_creation_input_tokens":0,
                "cache_read_input_tokens":0,"output_tokens":510}})")));
  EXPECT_EQ(accumulated.input_tokens, 10682);
  EXPECT_EQ(accumulated.output_tokens, 510);
}

TEST(ExtractAnthropicTest, MessageStartWithoutUsageIsTolerated) {
  const auto usage =
      extractUsage(ApiProtocol::AnthropicMessages,
                   parse(R"({"type":"message_start","message":{"model":"claude-opus-5"}})"));
  EXPECT_FALSE(usage.hasAny());
  EXPECT_EQ(usage.model, "claude-opus-5");
}

TEST(ExtractAnthropicTest, MessageStopIsTerminal) {
  EXPECT_TRUE(TokenUsageExtractor::isTerminalEvent(ApiProtocol::AnthropicMessages,
                                                   parse(R"({"type":"message_stop"})")));
  EXPECT_FALSE(TokenUsageExtractor::isTerminalEvent(ApiProtocol::AnthropicMessages,
                                                    parse(R"({"type":"message_delta"})")));
}

// ---------------------------------------------------------------------------
// Gemini extraction.

TEST(ExtractGeminiTest, UsageMetadata) {
  const auto usage = extractUsage(ApiProtocol::GeminiGenerateContent, parse(R"(
      {"candidates":[{"content":{"parts":[{"text":"hi"}],"role":"model"},"finishReason":"STOP"}],
       "usageMetadata":{"promptTokenCount":6,"candidatesTokenCount":149,"totalTokenCount":167,
                        "cachedContentTokenCount":2,"thoughtsTokenCount":12},
       "modelVersion":"gemini-2.5-flash"})"));
  EXPECT_EQ(usage.output_tokens, 149); // Native candidates count, thoughts excluded.
  TokenUsage finalized = usage;
  finalized.finalize();
  EXPECT_EQ(finalized.input_tokens, 6);
  // Canonical inclusive output: 149 candidates + 12 thoughts (Gemini reports
  // thoughts outside candidatesTokenCount).
  EXPECT_EQ(finalized.output_tokens, 161);
  // Computed canonical total (6 + 161) agrees with the provider total, which
  // is already inclusive; the provider total is preserved alongside.
  EXPECT_EQ(finalized.total_tokens, 167);
  EXPECT_EQ(finalized.provider_total_tokens, 167);
  EXPECT_EQ(finalized.cached_input_tokens, 2);
  EXPECT_EQ(finalized.reasoning_tokens, 12);
  EXPECT_EQ(finalized.model, "gemini-2.5-flash");
}

TEST(ExtractGeminiTest, ToolUseAndThoughtsAccounting) {
  // Reviewer merge-gate case: canonical input adds tool-use prompt tokens,
  // canonical output adds thoughts, and both match the provider total.
  auto usage = extractUsage(ApiProtocol::GeminiGenerateContent, parse(R"(
      {"usageMetadata":{"promptTokenCount":6,"toolUsePromptTokenCount":5,
                        "candidatesTokenCount":149,"thoughtsTokenCount":12,
                        "totalTokenCount":172}})"));
  usage.finalize();
  EXPECT_EQ(usage.input_tokens, 11);
  EXPECT_EQ(usage.output_tokens, 161);
  EXPECT_EQ(usage.total_tokens, 172);
  EXPECT_EQ(usage.provider_total_tokens, 172);
  EXPECT_EQ(usage.tool_use_input_tokens, 5);
  EXPECT_EQ(usage.reasoning_tokens, 12);
}

TEST(ExtractAnthropicTest, CanonicalInclusiveAccounting) {
  // Reviewer merge-gate case: input 100 + cache-creation 20 + cache-read 30
  // yields canonical input 150 and (with output 10) total 160.
  auto usage = extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"type":"message","usage":{"input_tokens":100,"cache_creation_input_tokens":20,
       "cache_read_input_tokens":30,"output_tokens":10}})"));
  usage.finalize();
  EXPECT_EQ(usage.input_tokens, 150);
  EXPECT_EQ(usage.cached_input_tokens, 30);
  EXPECT_EQ(usage.cache_creation_input_tokens, 20);
  EXPECT_EQ(usage.total_tokens, 160);
}

TEST(ExtractAnthropicTest, PartialInputUpdatePreservesCacheBuckets) {
  // Anthropic's streaming usage fields are independently optional and
  // cumulative. A later message_delta repeating input_tokens but omitting the
  // cache buckets must not regress the canonical inclusive input -- native
  // components accumulate independently and are summed only at finalize.
  TokenUsage accumulated;
  accumulated.merge(extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"type":"message_start","message":{"usage":{"input_tokens":100,
       "cache_read_input_tokens":30,"cache_creation_input_tokens":20,
       "output_tokens":1}}})")));
  accumulated.merge(extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"type":"message_delta","delta":{"stop_reason":"end_turn"},
       "usage":{"input_tokens":100,"output_tokens":50}})")));
  accumulated.finalize();
  EXPECT_EQ(accumulated.input_tokens, 150); // 100 + 30 + 20, not regressed to 100.
  EXPECT_EQ(accumulated.cached_input_tokens, 30);
  EXPECT_EQ(accumulated.cache_creation_input_tokens, 20);
  EXPECT_EQ(accumulated.output_tokens, 50);
  EXPECT_EQ(accumulated.total_tokens, 200);
}

TEST(ExtractGeminiTest, CumulativeChunksLastWins) {
  TokenUsage accumulated;
  accumulated.merge(extractUsage(ApiProtocol::GeminiGenerateContent, parse(R"(
      {"candidates":[{"content":{"parts":[{"text":"a"}]}}],
       "usageMetadata":{"promptTokenCount":6,"candidatesTokenCount":16,"totalTokenCount":22}})")));
  accumulated.merge(extractUsage(ApiProtocol::GeminiGenerateContent, parse(R"(
      {"candidates":[{"content":{"parts":[{"text":"b"}]},"finishReason":"STOP"}],
       "usageMetadata":{"promptTokenCount":6,"candidatesTokenCount":149,"totalTokenCount":155},
       "modelVersion":"gemini-2.5-flash"})")));
  accumulated.finalize();
  EXPECT_EQ(accumulated.output_tokens, 149);
  EXPECT_EQ(accumulated.total_tokens, 155);
  EXPECT_EQ(accumulated.model, "gemini-2.5-flash");
}

TEST(ExtractGeminiTest, ChunkWithoutUsageMetadataYieldsNothing) {
  const auto usage =
      extractUsage(ApiProtocol::GeminiGenerateContent,
                   parse(R"({"candidates":[{"content":{"parts":[{"text":"a"}]}}]})"));
  EXPECT_FALSE(usage.hasAny());
}

// ---------------------------------------------------------------------------
// Merge and normalization mechanics.

TEST(TokenUsageTest, PresentButInvalidFieldsAreFlaggedMalformed) {
  // A missing key is not malformed...
  EXPECT_FALSE(TokenUsageExtractor::extract(ApiProtocol::AnthropicMessages,
                                            parse(R"({"usage":{"output_tokens":5}})"))
                   .malformed);
  // ...but a known field that is present with an unusable value is: wrong
  // scalar type, wrong container type, negative, fractional, out of range,
  // and a non-object usage node itself.
  for (const std::string& body : {std::string(R"({"usage":{"output_tokens":"5"}})"),
                                  std::string(R"({"usage":{"output_tokens":{}}})"),
                                  std::string(R"({"usage":{"output_tokens":[5]}})"),
                                  std::string(R"({"usage":{"output_tokens":-5}})"),
                                  std::string(R"({"usage":{"output_tokens":5.5}})"),
                                  std::string(R"({"usage":{"output_tokens":9007199254740992}})"),
                                  std::string(R"({"usage":"not-an-object"})")}) {
    EXPECT_TRUE(TokenUsageExtractor::extract(ApiProtocol::AnthropicMessages, parse(body)).malformed)
        << body;
  }
  // Null handling is specific to the position. OpenAI documents `"usage": null` as the
  // placeholder on non-terminal chunks (and failed responses): benignly
  // absent, not malformed, and not degraded.
  EXPECT_FALSE(
      TokenUsageExtractor::extract(ApiProtocol::OpenAiChatCompletions, parse(R"({"usage":null})"))
          .malformed);
  EXPECT_FALSE(TokenUsageExtractor::extract(
                   ApiProtocol::OpenAiChatCompletions,
                   parse(R"({"usage":{"prompt_tokens":3,"prompt_tokens_details":null}})"))
                   .malformed);
  // A null in a *count* position is malformed in every dialect (the counters
  // are required integers): a corrupt final cumulative update must not leave
  // an earlier value published as complete.
  EXPECT_TRUE(TokenUsageExtractor::extract(ApiProtocol::AnthropicMessages,
                                           parse(R"({"usage":{"output_tokens":null}})"))
                  .malformed);
  EXPECT_TRUE(TokenUsageExtractor::extract(ApiProtocol::OpenAiChatCompletions,
                                           parse(R"({"usage":{"prompt_tokens":null}})"))
                  .malformed);
  // Null in a required structural position is malformed structure.
  EXPECT_TRUE(
      TokenUsageExtractor::extract(ApiProtocol::OpenAiChatCompletions,
                                   parse(R"({"type":"response.completed","response":null})"))
          .malformed);
  EXPECT_TRUE(TokenUsageExtractor::extract(ApiProtocol::AnthropicMessages,
                                           parse(R"({"type":"message_start","message":null})"))
                  .malformed);
  // Valid fields alongside a malformed one still extract.
  const auto result =
      TokenUsageExtractor::extract(ApiProtocol::AnthropicMessages,
                                   parse(R"({"usage":{"input_tokens":7,"output_tokens":"nope"}})"));
  EXPECT_TRUE(result.malformed);
  EXPECT_EQ(result.usage.input_tokens, 7);
}

TEST(TokenUsageTest, NullCountRegressionScenario) {
  // The reviewer reproduction: a final cumulative message_delta whose output_tokens
  // is null must flag the stream, so the surviving earlier count publishes as
  // partial rather than complete.
  TokenUsage accumulated;
  bool degraded = false;
  auto merge = [&](const std::string& body) {
    const auto result = TokenUsageExtractor::extract(ApiProtocol::AnthropicMessages, parse(body));
    accumulated.merge(result.usage);
    degraded |= result.malformed;
  };
  merge(R"({"type":"message_start","message":{"usage":{"input_tokens":10,"output_tokens":3}}})");
  merge(R"({"type":"message_delta","delta":{"stop_reason":"end_turn"},)"
        R"("usage":{"output_tokens":null}})");
  accumulated.finalize();
  EXPECT_EQ(accumulated.output_tokens, 3); // Earlier value survives...
  EXPECT_TRUE(degraded);                   // ...but the stream is flagged.
}

TEST(TokenUsageTest, CanonicalizationOverflowIsSurfaced) {
  // Individually valid fields summing above 2^53-1: the component is dropped
  // rather than published imprecisely, and the record is flagged so the
  // caller publishes partial instead of a silently non-canonical complete.
  auto usage = extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"usage":{"input_tokens":9007199254740991,"cache_read_input_tokens":1,
                "output_tokens":0}})"));
  usage.finalize();
  EXPECT_FALSE(usage.input_tokens.has_value());
  EXPECT_TRUE(usage.canonicalizationOverflow());

  auto fine = extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"usage":{"input_tokens":100,"cache_read_input_tokens":1,"output_tokens":0}})"));
  fine.finalize();
  EXPECT_FALSE(fine.canonicalizationOverflow());
}

TEST(TokenUsageTest, MergeAfterFinalizeIsRejected) {
  TokenUsage usage;
  usage.api_protocol = ApiProtocol::AnthropicMessages;
  usage.input_tokens = 1;
  usage.finalize();
  TokenUsage update;
  update.output_tokens = 2;
  EXPECT_DEBUG_DEATH(usage.merge(update), "");
}

TEST(TokenUsageTest, FinalizeIsSingleUse) {
  TokenUsage usage;
  usage.api_protocol = ApiProtocol::AnthropicMessages;
  usage.input_tokens = 100;
  usage.cached_input_tokens = 30;
  usage.cache_creation_input_tokens = 20;
  usage.output_tokens = 10;
  usage.finalize();
  EXPECT_EQ(usage.input_tokens, 150);
  EXPECT_EQ(usage.total_tokens, 160);
  // A second call must not re-add the cache buckets: it asserts in debug
  // builds and is a no-op in release builds.
  EXPECT_DEBUG_DEATH(usage.finalize(), "");
  EXPECT_EQ(usage.input_tokens, 150);
  EXPECT_EQ(usage.total_tokens, 160);
}

TEST(TokenUsageTest, MergeIncludesToolUseInputTokens) {
  // Every field the extractors populate must survive merge(): the Gemini
  // extractor reports tool_use_input_tokens, and all documents -- including a
  // single non-streaming JSON response -- pass through merge() before
  // publication.
  TokenUsage base;
  TokenUsage update;
  update.tool_use_input_tokens = 5;
  base.merge(update);
  EXPECT_EQ(base.tool_use_input_tokens, 5);
}

TEST(TokenUsageTest, MergeLaterNonNullWins) {
  TokenUsage base;
  base.input_tokens = 10;
  base.model = "a";

  TokenUsage update;
  update.output_tokens = 5;
  TokenUsage empty_update;

  base.merge(update);
  base.merge(empty_update); // An empty update must not clear anything.
  EXPECT_EQ(base.input_tokens, 10);
  EXPECT_EQ(base.output_tokens, 5);
  EXPECT_EQ(base.model, "a");
}

TEST(TokenUsageTest, FinalizeComputesCanonicalTotal) {
  TokenUsage usage;
  usage.input_tokens = 3;
  usage.output_tokens = 4;
  usage.finalize();
  EXPECT_EQ(usage.total_tokens, 7);
  EXPECT_FALSE(usage.provider_total_tokens.has_value());

  TokenUsage partial;
  partial.output_tokens = 4; // No input: the total cannot be computed.
  partial.finalize();
  EXPECT_FALSE(partial.total_tokens.has_value());

  TokenUsage partial_with_reported;
  partial_with_reported.output_tokens = 4;
  partial_with_reported.total_tokens = 9; // Native total; components incomplete.
  partial_with_reported.finalize();
  // The provider total never substitutes for the canonical sum; it is
  // preserved in its own field.
  EXPECT_FALSE(partial_with_reported.total_tokens.has_value());
  EXPECT_EQ(partial_with_reported.provider_total_tokens, 9);
}

TEST(TokenUsageTest, ProviderTotalPreservedSeparately) {
  // The canonical contract guarantees total == input + output. The provider's
  // own total is always preserved alongside, so a disagreeing one
  // (inconsistent response, or a bucket unknown to the extractor) never
  // breaks that invariant and stays observable.
  TokenUsage usage;
  usage.input_tokens = 3;
  usage.output_tokens = 4;
  usage.total_tokens = 100;
  usage.finalize();
  EXPECT_EQ(usage.total_tokens, 7);
  EXPECT_EQ(usage.provider_total_tokens, 100);

  // An agreeing provider total is preserved just the same.
  TokenUsage agreeing;
  agreeing.input_tokens = 3;
  agreeing.output_tokens = 4;
  agreeing.total_tokens = 7;
  agreeing.finalize();
  EXPECT_EQ(agreeing.total_tokens, 7);
  EXPECT_EQ(agreeing.provider_total_tokens, 7);
}

TEST(TokenUsageTest, NumericEdgeCases) {
  // Counts serialized as JSON doubles are accepted; negatives are ignored.
  const auto usage = extractUsage(ApiProtocol::AnthropicMessages,
                                  parse(R"({"usage":{"input_tokens":12.0,"output_tokens":-5}})"));
  EXPECT_EQ(usage.input_tokens, 12);
  EXPECT_FALSE(usage.output_tokens.has_value());
}

TEST(TokenUsageTest, UntrustedNumericValuesRejected) {
  // Fractional, astronomically large, out-of-double-precision, and wrong-typed
  // values are rejected rather than coerced: these feed billing/accounting
  // metadata, and casting e.g. 1e300 to an integer is undefined behavior.
  const auto usage = extractUsage(ApiProtocol::AnthropicMessages, parse(R"(
      {"usage":{"input_tokens":12.5,
                "output_tokens":1e300,
                "cache_read_input_tokens":9007199254740992,
                "cache_creation_input_tokens":9007199254740991,
                "output_tokens_details":{"thinking_tokens":"7"}}})"));
  EXPECT_FALSE(usage.input_tokens.has_value());  // fractional
  EXPECT_FALSE(usage.output_tokens.has_value()); // not representable
  // 2^53 is beyond the exactly-representable bound; 2^53-1 is the maximum.
  EXPECT_FALSE(usage.cached_input_tokens.has_value());
  EXPECT_EQ(usage.cache_creation_input_tokens, uint64_t(9007199254740991));
  EXPECT_FALSE(usage.reasoning_tokens.has_value()); // string, not a number
}

TEST(TokenUsageTest, ComputedTotalPublishedOnlyWhenDoubleExact) {
  // readCount caps each addend at 2^53-1, so the sum cannot overflow uint64_t;
  // but a sum above 2^53-1 may not be exactly representable by the
  // double-backed metadata field (e.g. an odd sum above 2^53 rounds), so the
  // computed total is published only within the same metadata-safe bound as
  // every other field.
  TokenUsage in_range;
  in_range.input_tokens = uint64_t(9007199254740000);
  in_range.output_tokens = uint64_t(991);
  in_range.finalize();
  EXPECT_EQ(in_range.total_tokens, uint64_t(9007199254740991)); // == 2^53-1, exact.

  TokenUsage out_of_range;
  out_of_range.input_tokens = uint64_t(9007199254740991);  // 2^53-1
  out_of_range.output_tokens = uint64_t(9007199254740990); // sum = 2^54-3, odd, inexact
  out_of_range.finalize();
  EXPECT_FALSE(out_of_range.total_tokens.has_value());

  // With both components present but the sum unpublishable, a provider total
  // must not slip in as total_tokens (it would necessarily violate
  // total == input + output); it stays in provider_total_tokens.
  TokenUsage out_of_range_reported;
  out_of_range_reported.input_tokens = uint64_t(9007199254740991);
  out_of_range_reported.output_tokens = uint64_t(9007199254740991);
  out_of_range_reported.total_tokens = 5;
  out_of_range_reported.finalize();
  EXPECT_FALSE(out_of_range_reported.total_tokens.has_value());
  EXPECT_EQ(out_of_range_reported.provider_total_tokens, 5);
}

TEST(TokenUsageTest, OversizedModelNameOmitted) {
  // The response-reported model is upstream-controlled: values beyond a small
  // bound are dropped so metadata and access-log entries stay small.
  const auto usage =
      extractUsage(ApiProtocol::AnthropicMessages,
                   parse("{\"type\":\"message\",\"model\":\"" + std::string(5000, 'm') +
                         "\",\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}"));
  EXPECT_TRUE(usage.model.empty());
  EXPECT_EQ(usage.input_tokens, 5); // Counts are unaffected.
}

TEST(TokenUsageTest, SecondaryOnlyCountsStillPublish) {
  // A usage object carrying only cache/reasoning counts is still a result.
  const auto usage = extractUsage(ApiProtocol::AnthropicMessages,
                                  parse(R"({"usage":{"cache_read_input_tokens":64}})"));
  EXPECT_TRUE(usage.hasAny());
  EXPECT_EQ(usage.cached_input_tokens, 64);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
