#include <optional>

#include "source/extensions/filters/http/ai_protocol_manager/api_bindings.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

std::optional<LlmBindingRead> peek(LLMProtocol protocol, absl::string_view path,
                                   const nlohmann::json& body = nlohmann::json::object(),
                                   const LlmBindingConstSharedPtr& binding = nullptr) {
  return peekClientPath(protocol, binding, path, body);
}

TEST(ApiBindingsTest, EachApiHasItsOwnLayouts) {
  const ApiBindings& openai_chat = apiBindings(LLMProtocol::OpenAiChatCompletions);
  ASSERT_NE(openai_chat.api, nullptr);
  ASSERT_TRUE(openai_chat.api->pattern().has_value());
  EXPECT_EQ(openai_chat.api->pattern()->source(), "/v1/chat/completions");
  EXPECT_TRUE(openai_chat.client.empty());
  const ApiBindings& anthropic = apiBindings(LLMProtocol::AnthropicMessages);
  ASSERT_NE(anthropic.api, nullptr);
  EXPECT_EQ(anthropic.client.size(), 1);
  const ApiBindings& gemini = apiBindings(LLMProtocol::GeminiGenerateContent);
  ASSERT_NE(gemini.api, nullptr);
  EXPECT_TRUE(gemini.api->removesModel());
  EXPECT_EQ(gemini.client.size(), 2);
  // APIs without a built-in binding return empty bindings.
  for (const LLMProtocol protocol : {LLMProtocol::OpenAiResponses, LLMProtocol::Unspecified}) {
    EXPECT_EQ(apiBindings(protocol).api, nullptr);
    EXPECT_TRUE(apiBindings(protocol).client.empty());
  }
  // One instance per API, so the engine and the filters share it.
  EXPECT_EQ(&apiBindings(LLMProtocol::GeminiGenerateContent), &gemini);

  // OpenAI's own path names neither model nor streaming mode: the body's stand.
  const std::optional<LlmBindingRead> chat_read =
      peek(LLMProtocol::OpenAiChatCompletions, "/v1/chat/completions",
           nlohmann::json::parse(R"({"model": "gpt-4o"})"));
  ASSERT_TRUE(chat_read.has_value());
  EXPECT_EQ(chat_read->binding, openai_chat.api);
  EXPECT_TRUE(chat_read->model.empty());
  EXPECT_FALSE(chat_read->model_from_path);
  EXPECT_FALSE(chat_read->stream.has_value());
}

TEST(ApiBindingsTest, ClaudeOnVertexNamesTheModelInThePath) {
  const nlohmann::json body =
      nlohmann::json::parse(R"({"anthropic_version": "vertex-2023-10-16", "max_tokens": 1})");
  std::optional<LlmBindingRead> read =
      peek(LLMProtocol::AnthropicMessages,
           "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/"
           "claude-sonnet-4-5@20250929:rawPredict",
           body);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->model, "claude-sonnet-4-5@20250929");
  EXPECT_TRUE(read->model_from_path);
  EXPECT_FALSE(read->stream.has_value());
  EXPECT_EQ(read->path_variables.at("project"), "acme");
  EXPECT_EQ(read->written_fields.at("anthropic_version"), "vertex-2023-10-16");

  read = peek(LLMProtocol::AnthropicMessages,
              "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/"
              "claude-sonnet-4-5@20250929:streamRawPredict",
              body);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->stream, true);

  // The provider's own path names no model: the body's stands.
  read = peek(LLMProtocol::AnthropicMessages, "/v1/messages",
              nlohmann::json::parse(R"({"model": "claude-opus-5"})"));
  ASSERT_TRUE(read.has_value());
  EXPECT_TRUE(read->model.empty());
  EXPECT_FALSE(read->model_from_path);
}

TEST(ApiBindingsTest, GeminiNamesTheModelAndTheModeInThePathUnderAnyVersion) {
  for (const absl::string_view path :
       {"/v1beta/models/gemini-2.5-pro:streamGenerateContent?alt=sse",
        "/v1/models/gemini-2.5-pro:streamGenerateContent",
        "/v1alpha/models/gemini-2.5-pro:streamGenerateContent",
        "/v1/projects/p/locations/us-central1/publishers/google/models/"
        "gemini-2.5-pro:streamGenerateContent?alt=sse",
        // Vertex AI express mode: no project or location.
        "/v1/publishers/google/models/gemini-2.5-pro:streamGenerateContent",
        "/v1beta1/publishers/google/models/gemini-2.5-pro:streamGenerateContent?alt=sse"}) {
    SCOPED_TRACE(path);
    const std::optional<LlmBindingRead> read = peek(LLMProtocol::GeminiGenerateContent, path);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->model, "gemini-2.5-pro");
    EXPECT_TRUE(read->model_from_path);
    EXPECT_EQ(read->stream, true);
  }
  const std::optional<LlmBindingRead> unary =
      peek(LLMProtocol::GeminiGenerateContent, "/v1beta/models/gemini-2.5-pro:generateContent");
  ASSERT_TRUE(unary.has_value());
  EXPECT_EQ(unary->stream, false);
  for (const absl::string_view path :
       {"/v1beta/models/gemini-2.5-pro:countTokens", "/v1beta/gemini-2.5-pro:generateContent",
        "/v1beta/models/gemini-2.5-pro", "/v1beta/models/:generateContent", "/",
        "/v1/chat/completions", "/v1/publishers/google/models/gemini-2.5-pro:countTokens",
        "/v1/publishers/anthropic/models/gemini-2.5-pro:generateContent"}) {
    EXPECT_FALSE(peek(LLMProtocol::GeminiGenerateContent, path).has_value()) << path;
  }
}

// A path under a gateway prefix is not one of the API's layouts; the route's binding names it.
TEST(ApiBindingsTest, GeminiUnderAPrefixNeedsABinding) {
  constexpr absl::string_view path = "/gemini/v1beta/models/gemini-2.5-pro:generateContent";
  EXPECT_FALSE(peek(LLMProtocol::GeminiGenerateContent, path).has_value());
  const absl::StatusOr<LlmBindingConstSharedPtr> prefixed = LlmBinding::parse(
      "/gemini/{version}/models/{model}:{generateContent|streamGenerateContent?alt=sse}");
  ASSERT_TRUE(prefixed.ok());
  const std::optional<LlmBindingRead> read =
      peek(LLMProtocol::GeminiGenerateContent, path, nlohmann::json::object(), *prefixed);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->model, "gemini-2.5-pro");
  EXPECT_TRUE(read->model_from_path);
  EXPECT_EQ(read->stream, false);
}

TEST(ApiBindingsTest, AGivenBindingReplacesTheApis) {
  const absl::StatusOr<LlmBindingConstSharedPtr> gateway = LlmBinding::parse("/gw/{model}");
  ASSERT_TRUE(gateway.ok());
  EXPECT_FALSE(peek(LLMProtocol::GeminiGenerateContent, "/v1beta/models/g:generateContent",
                    nlohmann::json::object(), *gateway)
                   .has_value());
  const std::optional<LlmBindingRead> read =
      peek(LLMProtocol::GeminiGenerateContent, "/gw/g", nlohmann::json::object(), *gateway);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(read->model, "g");
  // Gemini's own edits apply under a binding with only a pattern.
  EXPECT_EQ(read->binding, *gateway);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
