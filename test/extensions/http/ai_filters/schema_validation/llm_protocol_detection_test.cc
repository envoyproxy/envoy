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

LLMProtocol fromPath(const std::string& path) {
  return detectFromHeaders(Http::TestRequestHeaderMapImpl{{":path", path}});
}

LLMProtocol fromPayload(const std::string& json) {
  return detectFromPayload(nlohmann::json::parse(json));
}

TEST(DetectFromHeadersTest, MatchesProviderPathSuffix) {
  EXPECT_EQ(fromPath("/v1/chat/completions"), LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(fromPath("/openai/v1/chat/completions?trace=1"), LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(fromPath("/v1/responses"), LLMProtocol::OpenAiResponses);
  EXPECT_EQ(fromPath("/proxy/v1/messages"), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(fromPath("/v1beta/models/gemini-2.5-pro:generateContent"),
            LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(fromPath("/v1beta/models/gemini-2.5-pro:streamGenerateContent?alt=sse"),
            LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(fromPath("/v2/messages"), LLMProtocol::Unspecified);
  EXPECT_EQ(fromPath("/"), LLMProtocol::Unspecified);
}

TEST(DetectFromHeadersTest, PathWinsOverTheAnthropicVersionHeader) {
  EXPECT_EQ(detectFromHeaders(Http::TestRequestHeaderMapImpl{{":path", "/generate"},
                                                             {"anthropic-version", "2023-06-01"}}),
            LLMProtocol::AnthropicMessages);
  EXPECT_EQ(detectFromHeaders(Http::TestRequestHeaderMapImpl{{":path", "/v1/chat/completions"},
                                                             {"anthropic-version", "2023-06-01"}}),
            LLMProtocol::OpenAiChatCompletions);
}

TEST(DetectFromPayloadTest, ExclusiveMarkers) {
  EXPECT_EQ(fromPayload(R"({"contents":[{"parts":[{"text":"hi"}]}]})"),
            LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(fromPayload(R"({"system_instruction":{}})"), LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(fromPayload(R"({"model":"m","input":"hi"})"), LLMProtocol::OpenAiResponses);
  for (const std::string body : {
           R"({"messages":[],"system":"be terse"})",
           R"({"messages":[],"top_k":5})",
           R"({"messages":[],"tools":[{"name":"t","input_schema":{}}]})",
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

TEST(DetectFromPayloadTest, AmbiguousOrForeignDetectsNothing) {
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
