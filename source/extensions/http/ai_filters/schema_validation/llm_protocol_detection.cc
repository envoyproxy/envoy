#include "source/extensions/http/ai_filters/schema_validation/llm_protocol_detection.h"

#include "source/common/common/macros.h"
#include "source/common/http/path_utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/types/span.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace SchemaValidation {

using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::readString;
namespace Keys = HttpFilters::AiProtocolManager::Keys;

namespace {

const Http::LowerCaseString& anthropicVersionHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "anthropic-version");
}

constexpr absl::string_view GeminiOnlyKeys[]{Keys::Contents, "systemInstruction",
                                             "system_instruction", Keys::GenerationConfig,
                                             Keys::GenerationConfigSnake};
constexpr absl::string_view AnthropicOnlyKeys[]{"system", "stop_sequences", "top_k", "thinking"};
constexpr absl::string_view OpenAiOnlyKeys[]{Keys::MaxCompletionTokens, "response_format",
                                             "frequency_penalty",       "presence_penalty",
                                             "stream_options",          "logit_bias"};

bool hasAnyKey(const nlohmann::json& json, absl::Span<const absl::string_view> keys) {
  return absl::c_any_of(keys, [&json](absl::string_view key) { return json.contains(key); });
}

// Anthropic declares a tool's schema inline as `input_schema`; OpenAI nests it under `function`.
bool anyToolHas(const nlohmann::json& json, absl::string_view key) {
  const auto tools = json.find(Keys::Tools);
  return tools != json.end() && tools->is_array() &&
         absl::c_any_of(*tools, [key](const nlohmann::json& tool) {
           return tool.is_object() && tool.contains(key);
         });
}

// Anthropic admits only `user` and `assistant` turns.
bool hasOpenAiOnlyRole(const nlohmann::json& messages) {
  return messages.is_array() && absl::c_any_of(messages, [](const nlohmann::json& message) {
           const auto role = readString(message, Keys::Role);
           return role == "system" || role == "developer" || role == "tool";
         });
}

// `/v1/messages/count_tokens` and the like take a body shaped like the API they sit under.
bool isApiSubResource(absl::string_view path) {
  static constexpr absl::string_view ApiPaths[]{"/chat/completions/", "/responses/",
                                                "/v1/messages/"};
  return absl::c_any_of(
      ApiPaths, [path](absl::string_view api_path) { return absl::StrContains(path, api_path); });
}

bool endsInMethod(absl::string_view path) {
  return absl::StrContains(path.substr(path.rfind('/') + 1), ':');
}

} // namespace

LLMProtocol detectLlmProtocol(const Http::RequestHeaderMap& headers, const nlohmann::json& json) {
  struct PathSuffix {
    absl::string_view suffix;
    LLMProtocol protocol;
  };
  // Gemini names the model in the path, so only its method is fixed.
  static constexpr PathSuffix PathSuffixes[]{
      {"/chat/completions", LLMProtocol::OpenAiChatCompletions},
      {"/responses", LLMProtocol::OpenAiResponses},
      {"/v1/messages", LLMProtocol::AnthropicMessages},
      {":generateContent", LLMProtocol::GeminiGenerateContent},
      {":streamGenerateContent", LLMProtocol::GeminiGenerateContent},
  };
  const absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
  for (const auto& [suffix, protocol] : PathSuffixes) {
    if (absl::EndsWith(path, suffix)) {
      return protocol;
    }
  }
  if (!json.is_object() || isApiSubResource(path)) {
    return LLMProtocol::Unspecified;
  }
  if (hasAnyKey(json, GeminiOnlyKeys)) {
    // `:countTokens` and Gemini's other methods take `contents` too.
    return endsInMethod(path) ? LLMProtocol::Unspecified : LLMProtocol::GeminiGenerateContent;
  }
  // `input` alone is `/v1/embeddings` or `/v1/moderations` as much as the Responses API.
  const auto messages = json.find(Keys::Messages);
  if (messages == json.end()) {
    return LLMProtocol::Unspecified;
  }
  // Anthropic clients send the header to every endpoint, so it counts only beside `messages`.
  const bool anthropic = hasAnyKey(json, AnthropicOnlyKeys) || anyToolHas(json, "input_schema") ||
                         !headers.get(anthropicVersionHeader()).empty();
  const bool openai = hasAnyKey(json, OpenAiOnlyKeys) || anyToolHas(json, "function") ||
                      hasOpenAiOnlyRole(*messages);
  if (anthropic == openai) {
    return LLMProtocol::Unspecified;
  }
  if (openai) {
    return LLMProtocol::OpenAiChatCompletions;
  }
  // Messages requires `max_tokens`, which the token-counting endpoints shaped like it do not take.
  return json.contains(Keys::MaxTokens) ? LLMProtocol::AnthropicMessages : LLMProtocol::Unspecified;
}

} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
