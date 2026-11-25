#include <string>

#include "source/extensions/http/ai_filters/schema_validation/llm_protocol_detection.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace SchemaValidation {
namespace {

using HttpFilters::AiProtocolManager::LLMProtocol;

constexpr absl::string_view EitherChatApiPayload =
    R"({"model":"m","max_tokens":8,"messages":[{"role":"user","content":"hi"}]})";

LLMProtocol detect(absl::string_view path, absl::string_view payload,
                   bool anthropic_version = false) {
  Http::TestRequestHeaderMapImpl headers{{":path", std::string(path)}};
  if (anthropic_version) {
    headers.addCopy("anthropic-version", "2023-06-01");
  }
  return detectLlmProtocol(headers, nlohmann::json::parse(payload));
}

LLMProtocol fromPath(const std::string& path) { return detect(path, "{}"); }

LLMProtocol fromPayload(const std::string& json) { return detect("/generate", json); }

TEST(DetectLlmProtocolTest, MatchesProviderPathSuffix) {
  EXPECT_EQ(fromPath("/v1/chat/completions"), LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(fromPath("/openai/v1/chat/completions?trace=1"), LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(fromPath("/v1/responses"), LLMProtocol::OpenAiResponses);
  EXPECT_EQ(fromPath("/proxy/v1/messages"), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(fromPath("/v1/messages"), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(fromPath("/v1/messages?beta=true"), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(fromPath("/v1beta/models/gemini-2.5-pro:generateContent"),
            LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(fromPath("/v1beta/models/gemini-2.5-pro:streamGenerateContent?alt=sse"),
            LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(fromPath("/v2/messages"), LLMProtocol::Unspecified);
  EXPECT_EQ(fromPath("/"), LLMProtocol::Unspecified);
}

// `/v1/messages/count_tokens` takes a Messages body without the `max_tokens` that API requires.
TEST(DetectLlmProtocolTest, PathUnderAnApiPathDetectsNothing) {
  for (const absl::string_view path : {
           "/v1/messages/count_tokens",
           "/anthropic/v1/messages/count_tokens?beta=true",
           "/v1/messages/batches",
           "/anthropic/v1/messages/batches",
           "/v1/responses/input_tokens",
           "/v1/chat/completions/chatcmpl-1",
       }) {
    for (const absl::string_view payload : {
             R"({"model":"m","system":"be terse","messages":[{"role":"user","content":"hi"}]})",
             R"({"requests":[]})",
             R"({"model":"m","input":"hi"})",
             R"({"messages":[{"role":"system","content":"be terse"}]})",
             R"({"contents":[{"parts":[{"text":"hi"}]}]})",
         }) {
      for (const bool anthropic_version : {false, true}) {
        EXPECT_EQ(detect(path, payload, anthropic_version), LLMProtocol::Unspecified)
            << path << " " << payload << " anthropic-version: " << anthropic_version;
      }
    }
  }
}

// `:rawPredict` is how Vertex AI serves Claude, so another method discounts Gemini's keys only.
TEST(DetectLlmProtocolTest, GeminiPayloadOnAnotherMethodDetectsNothing) {
  constexpr absl::string_view payload = R"({"contents":[{"parts":[{"text":"hi"}]}]})";
  EXPECT_EQ(detect("/generate", payload), LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(detect("/v1beta/models/gemini-2.5-pro:countTokens", payload), LLMProtocol::Unspecified);
  EXPECT_EQ(detect("/v1/publishers/anthropic/models/claude:rawPredict",
                   R"({"max_tokens":8,"system":"be terse","messages":[]})"),
            LLMProtocol::AnthropicMessages);
}

// Messages requires `max_tokens`. Token counting takes the same body without it, and for Claude
// on Vertex AI the path names no API.
TEST(DetectLlmProtocolTest, MessagesPayloadWithoutMaxTokensDetectsNothing) {
  constexpr absl::string_view payload =
      R"({"model":"m","system":"be terse","messages":[{"role":"user","content":"hi"}]})";
  for (const absl::string_view path : {
           "/v1/projects/p/locations/us-east5/publishers/anthropic/models/count-tokens:rawPredict",
           "/generate",
       }) {
    for (const bool anthropic_version : {false, true}) {
      EXPECT_EQ(detect(path, payload, anthropic_version), LLMProtocol::Unspecified)
          << path << " anthropic-version: " << anthropic_version;
    }
  }
}

// Anthropic clients send the header to every endpoint, so it names the provider, not the API.
TEST(DetectLlmProtocolTest, AnthropicVersionHeaderOnlyBreaksATie) {
  EXPECT_EQ(detect("/generate", EitherChatApiPayload), LLMProtocol::Unspecified);
  EXPECT_EQ(detect("/generate", EitherChatApiPayload, true), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(detect("/generate", R"({"prompt":"hi"})", true), LLMProtocol::Unspecified);
  EXPECT_EQ(detect("/v1/complete", R"({"model":"m","prompt":"hi","max_tokens_to_sample":8})", true),
            LLMProtocol::Unspecified);
  EXPECT_EQ(detect("/generate", R"({"messages":[],"response_format":{}})", true),
            LLMProtocol::Unspecified);
  EXPECT_EQ(detect("/v1/chat/completions", EitherChatApiPayload, true),
            LLMProtocol::OpenAiChatCompletions);
}

// `/v1/embeddings`, `/v1/moderations` and `/v1/audio/speech` take a `model` and an `input` too.
TEST(DetectLlmProtocolTest, ResponsesIsDetectedByPathOnly) {
  constexpr absl::string_view payload = R"({"model":"m","input":"hi"})";
  EXPECT_EQ(detect("/v1/responses", payload), LLMProtocol::OpenAiResponses);
  for (const absl::string_view path :
       {"/v1/embeddings", "/v1/moderations", "/v1/audio/speech", "/generate"}) {
    EXPECT_EQ(detect(path, payload), LLMProtocol::Unspecified) << path;
  }
}

TEST(DetectLlmProtocolTest, ExclusivePayloadMarkers) {
  EXPECT_EQ(fromPayload(R"({"contents":[{"parts":[{"text":"hi"}]}]})"),
            LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(fromPayload(R"({"system_instruction":{}})"), LLMProtocol::GeminiGenerateContent);
  for (const std::string body : {
           R"({"max_tokens":8,"messages":[],"system":"be terse"})",
           R"({"max_tokens":8,"messages":[],"top_k":5})",
           R"({"max_tokens":8,"messages":[],"tools":[{"name":"t","input_schema":{}}]})",
       }) {
    EXPECT_EQ(fromPayload(body), LLMProtocol::AnthropicMessages) << body;
  }
  for (const std::string body : {
           R"({"messages":[],"max_completion_tokens":8})",
           R"({"messages":[],"tools":[{"type":"function","function":{"name":"t"}}]})",
           R"({"messages":[{"role":"system","content":"be terse"}]})",
       }) {
    EXPECT_EQ(fromPayload(body), LLMProtocol::OpenAiChatCompletions) << body;
  }
}

TEST(DetectLlmProtocolTest, AmbiguousOrForeignPayloadDetectsNothing) {
  for (const std::string body : {
           R"({"model":"m","max_tokens":8,"messages":[{"role":"user","content":"hi"}]})",
           R"({"messages":[],"top_k":5,"response_format":{}})",
           R"({"messages":"hi","tools":"t"})",
           R"({"model":"m"})",
           R"([1,2,3])",
       }) {
    EXPECT_EQ(fromPayload(body), LLMProtocol::Unspecified) << body;
  }
}

} // namespace
} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
