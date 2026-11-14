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
  const ApiBindings& anthropic = apiBindings(LLMProtocol::AnthropicMessages);
  ASSERT_NE(anthropic.api, nullptr);
  EXPECT_EQ(anthropic.client.size(), 1);
  const ApiBindings& gemini = apiBindings(LLMProtocol::GeminiGenerateContent);
  ASSERT_NE(gemini.api, nullptr);
  EXPECT_TRUE(gemini.api->removesModel());
  EXPECT_EQ(gemini.client.size(), 1);
  // The OpenAI API carries everything in the body; an API without a pack has nothing either.
  for (const LLMProtocol protocol : {LLMProtocol::OpenAiChatCompletions,
                                     LLMProtocol::OpenAiResponses, LLMProtocol::Unspecified}) {
    EXPECT_EQ(apiBindings(protocol).api, nullptr);
    EXPECT_TRUE(apiBindings(protocol).client.empty());
  }
  // One instance per API, so the engine and the filters share it.
  EXPECT_EQ(&apiBindings(LLMProtocol::GeminiGenerateContent), &gemini);
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
        "gemini-2.5-pro:streamGenerateContent?alt=sse"}) {
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
        "/v1/chat/completions"}) {
    EXPECT_FALSE(peek(LLMProtocol::GeminiGenerateContent, path).has_value()) << path;
  }
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
