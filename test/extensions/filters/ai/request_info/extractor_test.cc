#include <string>

#include "source/extensions/filters/ai/request_info/extractor.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {
namespace {

using HttpFilters::AiProtocolManager::ApiProtocol;
using HttpFilters::AiProtocolManager::apiProtocolName;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::MaxStringValueSize;

nlohmann::json parse(const std::string& json) {
  nlohmann::json result = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  EXPECT_FALSE(result.is_discarded()) << json;
  return result;
}

RequestAttributes extract(ApiProtocol protocol, const std::string& json,
                          absl::string_view path = "/v1/chat/completions") {
  return extractRequestAttributes(protocol, parse(json), path);
}

TEST(RequestInfoExtractorTest, OpenAiChatCompletions) {
  const RequestAttributes attrs = extract(
      ApiProtocol::OpenAiChatCompletions,
      R"({"model":"gpt-4o","stream":true,"max_tokens":128,
          "messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"yo"}],
          "tools":[{"type":"function","function":{"name":"f"}}]})");
  EXPECT_EQ(attrs.api_protocol, ApiProtocol::OpenAiChatCompletions);
  EXPECT_EQ(attrs.model, "gpt-4o");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 128u);
  EXPECT_EQ(attrs.message_count, 2u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiChatCompletionsPrefersMaxCompletionTokens) {
  EXPECT_EQ(extract(ApiProtocol::OpenAiChatCompletions,
                    R"({"max_completion_tokens":64,"max_tokens":128})")
                .max_output_tokens,
            64u);
  EXPECT_EQ(extract(ApiProtocol::OpenAiChatCompletions, R"({"max_tokens":128})").max_output_tokens,
            128u);
}

TEST(RequestInfoExtractorTest, OpenAiResponses) {
  const RequestAttributes attrs = extract(
      ApiProtocol::OpenAiResponses,
      R"({"model":"gpt-5","stream":false,"max_output_tokens":50,
          "input":[{"role":"user","content":"a"},{"role":"user","content":"b"},
                   {"role":"user","content":"c"}],
          "tools":[{"type":"web_search"},{"type":"function","name":"f"}]})",
      "/v1/responses");
  EXPECT_EQ(attrs.api_protocol, ApiProtocol::OpenAiResponses);
  EXPECT_EQ(attrs.model, "gpt-5");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_EQ(attrs.max_output_tokens, 50u);
  EXPECT_EQ(attrs.message_count, 3u);
  EXPECT_EQ(attrs.tool_count, 2u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiResponsesStringInputIsOneMessage) {
  const RequestAttributes attrs =
      extract(ApiProtocol::OpenAiResponses, R"({"model":"gpt-5","input":"hello"})");
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiResponsesOffloadedStringInputIsOneMessage) {
  nlohmann::json json = parse(R"({"model":"gpt-5"})");
  json["input"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 2000});
  const RequestAttributes attrs =
      extractRequestAttributes(ApiProtocol::OpenAiResponses, json, "/v1/responses");
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, AnthropicMessages) {
  const RequestAttributes attrs = extract(
      ApiProtocol::AnthropicMessages,
      R"({"model":"claude-opus-5","max_tokens":1024,"stream":true,"system":"be nice",
          "messages":[{"role":"user","content":"hi"}],
          "tools":[{"name":"t","input_schema":{"type":"object"}}]})",
      "/v1/messages");
  EXPECT_EQ(attrs.api_protocol, ApiProtocol::AnthropicMessages);
  EXPECT_EQ(attrs.model, "claude-opus-5");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 1024u);
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiReadsTargetAndGenerationConfig) {
  const RequestAttributes attrs = extract(
      ApiProtocol::GeminiGenerateContent,
      R"({"contents":[{"role":"user","parts":[{"text":"hi"}]},
                      {"role":"model","parts":[{"text":"yo"}]}],
          "generationConfig":{"maxOutputTokens":64},
          "tools":[{"functionDeclarations":[]}]})",
      "/v1beta/models/gemini-2.5-pro:streamGenerateContent?alt=sse");
  EXPECT_EQ(attrs.api_protocol, ApiProtocol::GeminiGenerateContent);
  EXPECT_EQ(attrs.model, "gemini-2.5-pro");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 64u);
  EXPECT_EQ(attrs.message_count, 2u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiAcceptsSnakeCaseAndVertexTarget) {
  const RequestAttributes attrs = extract(
      ApiProtocol::GeminiGenerateContent,
      R"({"contents":[],"generation_config":{"max_output_tokens":32}})",
      "/v1/projects/p/locations/us-central1/publishers/google/models/gemini-2.5-flash:generateContent");
  EXPECT_EQ(attrs.model, "gemini-2.5-flash");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_EQ(attrs.max_output_tokens, 32u);
  EXPECT_EQ(attrs.message_count, 0u);
  EXPECT_FALSE(attrs.malformed);
}

// Another operation, a bare model, or an empty model is not a generate target.
TEST(RequestInfoExtractorTest, GeminiIgnoresUnrecognizedTarget) {
  for (const absl::string_view path :
       {"/v1beta/models/gemini-2.5-pro:countTokens", "/v1beta/gemini-2.5-pro:generateContent",
        "/v1beta/models/gemini-2.5-pro", "/v1beta/models/:generateContent", "/"}) {
    const RequestAttributes attrs = extract(ApiProtocol::GeminiGenerateContent, "{}", path);
    EXPECT_TRUE(attrs.model.empty()) << path;
    EXPECT_FALSE(attrs.stream.has_value()) << path;
    EXPECT_FALSE(attrs.malformed) << path;
  }
}

TEST(RequestInfoExtractorTest, GeminiOversizedPathModelFlagsAndKeepsStream) {
  const RequestAttributes attrs =
      extract(ApiProtocol::GeminiGenerateContent, "{}",
              "/v1beta/models/" + std::string(MaxStringValueSize + 1, 'm') + ":streamGenerateContent");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_EQ(attrs.stream, true);
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, UnspecifiedReadsSharedAttributesOnly) {
  const RequestAttributes attrs = extract(
      ApiProtocol::Unspecified,
      R"({"model":"m","stream":false,"max_tokens":5,
          "messages":[{"role":"user","content":"hi"}],"tools":[]})");
  EXPECT_EQ(attrs.api_protocol, ApiProtocol::Unspecified);
  EXPECT_EQ(attrs.model, "m");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_FALSE(attrs.message_count.has_value());
  EXPECT_FALSE(attrs.tool_count.has_value());
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, AbsentAttributesStayAbsent) {
  for (const ApiProtocol protocol :
       {ApiProtocol::Unspecified, ApiProtocol::OpenAiChatCompletions, ApiProtocol::OpenAiResponses,
        ApiProtocol::AnthropicMessages, ApiProtocol::GeminiGenerateContent}) {
    const RequestAttributes attrs = extract(protocol, "{}", "/");
    EXPECT_EQ(attrs.api_protocol, protocol);
    EXPECT_TRUE(attrs.model.empty()) << apiProtocolName(protocol);
    EXPECT_FALSE(attrs.stream.has_value()) << apiProtocolName(protocol);
    EXPECT_FALSE(attrs.max_output_tokens.has_value()) << apiProtocolName(protocol);
    EXPECT_FALSE(attrs.message_count.has_value()) << apiProtocolName(protocol);
    EXPECT_FALSE(attrs.tool_count.has_value()) << apiProtocolName(protocol);
    EXPECT_FALSE(attrs.malformed) << apiProtocolName(protocol);
  }
}

TEST(RequestInfoExtractorTest, NullAttributesReadAsAbsent) {
  const RequestAttributes attrs = extract(
      ApiProtocol::OpenAiChatCompletions,
      R"({"model":"gpt-4o","stream":null,"max_tokens":null,"max_completion_tokens":null,
          "tools":null,"messages":[]})");
  EXPECT_EQ(attrs.model, "gpt-4o");
  EXPECT_FALSE(attrs.stream.has_value());
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_FALSE(attrs.tool_count.has_value());
  EXPECT_EQ(attrs.message_count, 0u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, UnusableAttributesReadAsAbsentAndFlag) {
  const RequestAttributes attrs = extract(
      ApiProtocol::OpenAiChatCompletions,
      R"({"model":42,"stream":"yes","max_tokens":-1,"messages":"nope","tools":{}})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_FALSE(attrs.stream.has_value());
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_FALSE(attrs.message_count.has_value());
  EXPECT_FALSE(attrs.tool_count.has_value());
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OneUnusableAttributeKeepsTheRest) {
  const RequestAttributes attrs = extract(
      ApiProtocol::AnthropicMessages,
      R"({"model":"claude-opus-5","max_tokens":"lots","messages":[{"role":"user","content":"hi"}]})",
      "/v1/messages");
  EXPECT_EQ(attrs.model, "claude-opus-5");
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_TRUE(attrs.malformed);
}

// A model offloaded to the external buffer is not readable from the index.
TEST(RequestInfoExtractorTest, OffloadedModelReadsAsAbsentAndFlags) {
  nlohmann::json json = parse(R"({"messages":[]})");
  json["model"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 20});
  const RequestAttributes attrs =
      extractRequestAttributes(ApiProtocol::AnthropicMessages, json, "/v1/messages");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_EQ(attrs.message_count, 0u);
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OversizedModelReadsAsAbsentAndFlags) {
  const RequestAttributes attrs =
      extract(ApiProtocol::OpenAiChatCompletions,
              R"({"model":")" + std::string(MaxStringValueSize + 1, 'm') + R"("})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, EmptyModelReadsAsAbsentWithoutFlag) {
  const RequestAttributes attrs = extract(ApiProtocol::OpenAiChatCompletions, R"({"model":""})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_FALSE(attrs.malformed);
}

} // namespace
} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
