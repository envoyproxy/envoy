#include <optional>
#include <string>

#include "source/extensions/filters/http/ai_protocol_manager/api_bindings.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/http/ai_filters/request_info/extractor.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {
namespace {

using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LlmBindingRead;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::llmProtocolName;
using HttpFilters::AiProtocolManager::MaxStringValueSize;
using HttpFilters::AiProtocolManager::peekClientPath;

nlohmann::json parse(const std::string& json) {
  nlohmann::json result = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  EXPECT_FALSE(result.is_discarded()) << json;
  return result;
}

// As the filter reads: the path under the API's own bindings, then the body.
RequestAttributes extractJson(LLMProtocol protocol, const nlohmann::json& json,
                              absl::string_view path = "/v1/chat/completions") {
  const std::optional<LlmBindingRead> arrived = peekClientPath(protocol, nullptr, path, json);
  return extractRequestAttributes(protocol, json, arrived.has_value() ? &*arrived : nullptr);
}

RequestAttributes extract(LLMProtocol protocol, const std::string& json,
                          absl::string_view path = "/v1/chat/completions") {
  return extractJson(protocol, parse(json), path);
}

TEST(RequestInfoExtractorTest, OpenAiChatCompletions) {
  const RequestAttributes attrs = extract(LLMProtocol::OpenAiChatCompletions,
                                          R"({"model":"gpt-4o","stream":true,"max_tokens":128,
          "messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"yo"}],
          "tools":[{"type":"function","function":{"name":"f"}}]})");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::OpenAiChatCompletions);
  EXPECT_EQ(attrs.model, "gpt-4o");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 128u);
  EXPECT_EQ(attrs.message_count, 2u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiChatCompletionsPrefersMaxCompletionTokens) {
  EXPECT_EQ(extract(LLMProtocol::OpenAiChatCompletions,
                    R"({"max_completion_tokens":64,"max_tokens":128})")
                .max_output_tokens,
            64u);
  EXPECT_EQ(extract(LLMProtocol::OpenAiChatCompletions, R"({"max_tokens":128})").max_output_tokens,
            128u);
}

TEST(RequestInfoExtractorTest, OpenAiResponses) {
  const RequestAttributes attrs = extract(LLMProtocol::OpenAiResponses,
                                          R"({"model":"gpt-5","stream":false,"max_output_tokens":50,
          "input":[{"role":"user","content":"a"},{"role":"user","content":"b"},
                   {"role":"user","content":"c"}],
          "tools":[{"type":"web_search"},{"type":"function","name":"f"}]})",
                                          "/v1/responses");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::OpenAiResponses);
  EXPECT_EQ(attrs.model, "gpt-5");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_EQ(attrs.max_output_tokens, 50u);
  EXPECT_EQ(attrs.message_count, 3u);
  EXPECT_EQ(attrs.tool_count, 2u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiResponsesStringInputIsOneMessage) {
  const RequestAttributes attrs =
      extract(LLMProtocol::OpenAiResponses, R"({"model":"gpt-5","input":"hello"})");
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OpenAiResponsesOffloadedStringInputIsOneMessage) {
  nlohmann::json json = parse(R"({"model":"gpt-5"})");
  json["input"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 2000});
  const RequestAttributes attrs = extractJson(LLMProtocol::OpenAiResponses, json, "/v1/responses");
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, AnthropicMessages) {
  const RequestAttributes attrs =
      extract(LLMProtocol::AnthropicMessages,
              R"({"model":"claude-opus-5","max_tokens":1024,"stream":true,"system":"be nice",
          "messages":[{"role":"user","content":"hi"}],
          "tools":[{"name":"t","input_schema":{"type":"object"}}]})",
              "/v1/messages");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::AnthropicMessages);
  EXPECT_EQ(attrs.model, "claude-opus-5");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 1024u);
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiReadsTargetAndGenerationConfig) {
  const RequestAttributes attrs =
      extract(LLMProtocol::GeminiGenerateContent,
              R"({"contents":[{"role":"user","parts":[{"text":"hi"}]},
                      {"role":"model","parts":[{"text":"yo"}]}],
          "generationConfig":{"maxOutputTokens":64},
          "tools":[{"functionDeclarations":[]}]})",
              "/v1beta/models/gemini-2.5-pro:streamGenerateContent?alt=sse");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(attrs.model, "gemini-2.5-pro");
  EXPECT_EQ(attrs.stream, true);
  EXPECT_EQ(attrs.max_output_tokens, 64u);
  EXPECT_EQ(attrs.message_count, 2u);
  EXPECT_EQ(attrs.tool_count, 1u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiAcceptsSnakeCaseAndVertexTarget) {
  const RequestAttributes attrs =
      extract(LLMProtocol::GeminiGenerateContent,
              R"({"contents":[],"generation_config":{"max_output_tokens":32}})",
              "/v1/projects/p/locations/us-central1/publishers/google/models/"
              "gemini-2.5-flash:generateContent");
  EXPECT_EQ(attrs.model, "gemini-2.5-flash");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_EQ(attrs.max_output_tokens, 32u);
  EXPECT_EQ(attrs.message_count, 0u);
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiIgnoresUnrecognizedTarget) {
  for (const absl::string_view path :
       {"/v1beta/models/gemini-2.5-pro:countTokens", "/v1beta/gemini-2.5-pro:generateContent",
        "/v1beta/models/gemini-2.5-pro", "/v1beta/models/:generateContent", "/"}) {
    const RequestAttributes attrs = extract(LLMProtocol::GeminiGenerateContent, "{}", path);
    EXPECT_TRUE(attrs.model.empty()) << path;
    EXPECT_FALSE(attrs.stream.has_value()) << path;
    EXPECT_FALSE(attrs.malformed) << path;
  }
}

TEST(RequestInfoExtractorTest, GeminiOversizedPathModelFlagsAndKeepsStream) {
  const RequestAttributes attrs = extract(
      LLMProtocol::GeminiGenerateContent, "{}",
      "/v1beta/models/" + std::string(MaxStringValueSize + 1, 'm') + ":streamGenerateContent");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_EQ(attrs.stream, true);
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, GeminiReadsTheTargetUnderAnyApiVersion) {
  const RequestAttributes attrs = extract(LLMProtocol::GeminiGenerateContent, "{}",
                                          "/v1/models/gemini-2.5-pro:generateContent");
  EXPECT_EQ(attrs.model, "gemini-2.5-pro");
  EXPECT_EQ(attrs.stream, false);
}

// Claude on Vertex AI names the model in the path and leaves `stream` to the body unless the
// path's spelling says otherwise.
TEST(RequestInfoExtractorTest, AnthropicReadsTheVertexTarget) {
  constexpr absl::string_view path =
      "/v1/projects/p/locations/us-east5/publishers/anthropic/models/claude-opus-5@20251001";
  RequestAttributes attrs =
      extract(LLMProtocol::AnthropicMessages,
              R"({"anthropic_version":"vertex-2023-10-16","max_tokens":64,"messages":[]})",
              absl::StrCat(path, ":rawPredict"));
  EXPECT_EQ(attrs.model, "claude-opus-5@20251001");
  EXPECT_FALSE(attrs.stream.has_value());
  EXPECT_EQ(attrs.max_output_tokens, 64u);
  EXPECT_FALSE(attrs.malformed);

  attrs = extract(LLMProtocol::AnthropicMessages, R"({"max_tokens":64})",
                  absl::StrCat(path, ":streamRawPredict"));
  EXPECT_EQ(attrs.model, "claude-opus-5@20251001");
  EXPECT_EQ(attrs.stream, true);

  // Vertex AI takes the model from the path alone, so the path wins over a body that names one.
  attrs = extract(LLMProtocol::AnthropicMessages, R"({"model":"from-body","stream":false})",
                  absl::StrCat(path, ":streamRawPredict"));
  EXPECT_EQ(attrs.model, "claude-opus-5@20251001");
  EXPECT_EQ(attrs.stream, false); // The body named it.
}

// A route's binding stands in for the API's: the extractor reads whatever it was given.
TEST(RequestInfoExtractorTest, ReadsTheGivenBinding) {
  const absl::StatusOr<HttpFilters::AiProtocolManager::LlmBindingConstSharedPtr> gateway =
      HttpFilters::AiProtocolManager::LlmBinding::parse("/gw/{model}/{chat|chat-stream}");
  ASSERT_TRUE(gateway.ok());
  const nlohmann::json json = parse(R"({"model":"from-body"})");
  std::optional<LlmBindingRead> arrived =
      peekClientPath(LLMProtocol::OpenAiChatCompletions, *gateway, "/gw/gpt-4o/chat-stream", json);
  ASSERT_TRUE(arrived.has_value());
  RequestAttributes attrs =
      extractRequestAttributes(LLMProtocol::OpenAiChatCompletions, json, &*arrived);
  EXPECT_EQ(attrs.model, "from-body"); // The binding keeps `model` in the body.
  EXPECT_EQ(attrs.stream, true);       // The path named it; the body did not.
  attrs = extractRequestAttributes(LLMProtocol::OpenAiChatCompletions, json, nullptr);
  EXPECT_FALSE(attrs.stream.has_value());
}

TEST(RequestInfoExtractorTest, UnspecifiedReadsSharedAttributesOnly) {
  const RequestAttributes attrs = extract(LLMProtocol::Unspecified,
                                          R"({"model":"m","stream":false,"max_tokens":5,
          "messages":[{"role":"user","content":"hi"}],"tools":[]})");
  EXPECT_EQ(attrs.llm_protocol, LLMProtocol::Unspecified);
  EXPECT_EQ(attrs.model, "m");
  EXPECT_EQ(attrs.stream, false);
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_FALSE(attrs.message_count.has_value());
  EXPECT_FALSE(attrs.tool_count.has_value());
  EXPECT_FALSE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, AbsentAttributesStayAbsent) {
  for (const LLMProtocol protocol :
       {LLMProtocol::Unspecified, LLMProtocol::OpenAiChatCompletions, LLMProtocol::OpenAiResponses,
        LLMProtocol::AnthropicMessages, LLMProtocol::GeminiGenerateContent}) {
    const RequestAttributes attrs = extract(protocol, "{}", "/");
    EXPECT_EQ(attrs.llm_protocol, protocol);
    EXPECT_TRUE(attrs.model.empty()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.stream.has_value()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.max_output_tokens.has_value()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.message_count.has_value()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.tool_count.has_value()) << llmProtocolName(protocol);
    EXPECT_FALSE(attrs.malformed) << llmProtocolName(protocol);
  }
}

TEST(RequestInfoExtractorTest, NullAttributesReadAsAbsent) {
  const RequestAttributes attrs =
      extract(LLMProtocol::OpenAiChatCompletions,
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
  const RequestAttributes attrs =
      extract(LLMProtocol::OpenAiChatCompletions,
              R"({"model":42,"stream":"yes","max_tokens":-1,"messages":"nope","tools":{}})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_FALSE(attrs.stream.has_value());
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_FALSE(attrs.message_count.has_value());
  EXPECT_FALSE(attrs.tool_count.has_value());
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OneUnusableAttributeKeepsTheRest) {
  const RequestAttributes attrs =
      extract(LLMProtocol::AnthropicMessages,
              R"({"model":"claude-opus-5","max_tokens":"lots","messages":[{"role":"user"}]})",
              "/v1/messages");
  EXPECT_EQ(attrs.model, "claude-opus-5");
  EXPECT_FALSE(attrs.max_output_tokens.has_value());
  EXPECT_EQ(attrs.message_count, 1u);
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OffloadedModelReadsAsAbsentAndFlags) {
  nlohmann::json json = parse(R"({"messages":[]})");
  json["model"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 20});
  const RequestAttributes attrs = extractJson(LLMProtocol::AnthropicMessages, json, "/v1/messages");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_EQ(attrs.message_count, 0u);
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, OversizedModelReadsAsAbsentAndFlags) {
  const RequestAttributes attrs =
      extract(LLMProtocol::OpenAiChatCompletions,
              R"({"model":")" + std::string(MaxStringValueSize + 1, 'm') + R"("})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_TRUE(attrs.malformed);
}

TEST(RequestInfoExtractorTest, EmptyModelReadsAsAbsentWithoutFlag) {
  const RequestAttributes attrs = extract(LLMProtocol::OpenAiChatCompletions, R"({"model":""})");
  EXPECT_TRUE(attrs.model.empty());
  EXPECT_FALSE(attrs.malformed);
}

} // namespace
} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
