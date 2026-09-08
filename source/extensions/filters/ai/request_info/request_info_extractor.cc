#include "source/extensions/filters/ai/request_info/request_info_extractor.h"

#include <limits>
#include <string>
#include <utility>

#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/strings/match.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

using HttpFilters::AiProtocolManager::ApiProtocol;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::NullPolicy;
using HttpFilters::AiProtocolManager::readCount;
using HttpFilters::AiProtocolManager::readObject;

namespace {

// Client-controlled strings; the cap keeps the published record small.
constexpr size_t MaxModelBytes = 256;

struct KeyValues {
  const std::string Model{"model"};
  const std::string Stream{"stream"};
  const std::string MaxTokens{"max_tokens"};
  const std::string MaxCompletionTokens{"max_completion_tokens"};
  const std::string MaxOutputTokens{"max_output_tokens"};
  const std::string MaxOutputTokensCamel{"maxOutputTokens"};
  const std::string GenerationConfig{"generationConfig"};
  const std::string GenerationConfigSnake{"generation_config"};
  const std::string Messages{"messages"};
  const std::string Input{"input"};
  const std::string Contents{"contents"};
  const std::string Tools{"tools"};
};
using Keys = ConstSingleton<KeyValues>;

// Null reads as absent throughout: these APIs document it as "unset".

std::optional<std::string> readString(const nlohmann::json& json, const std::string& key,
                                      bool& malformed) {
  const auto it = json.find(key);
  if (it == json.end() || it->is_null()) {
    return std::nullopt;
  }
  // An offloaded string is a binary node, so it lands here too.
  if (!it->is_string() || it->get_ref<const std::string&>().size() > MaxModelBytes) {
    malformed = true;
    return std::nullopt;
  }
  const std::string& value = it->get_ref<const std::string&>();
  return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

std::optional<bool> readBool(const nlohmann::json& json, const std::string& key, bool& malformed) {
  const auto it = json.find(key);
  if (it == json.end() || it->is_null()) {
    return std::nullopt;
  }
  if (!it->is_boolean()) {
    malformed = true;
    return std::nullopt;
  }
  return it->get<bool>();
}

std::optional<uint32_t> readArrayLength(const nlohmann::json& json, const std::string& key,
                                        bool& malformed) {
  const auto it = json.find(key);
  if (it == json.end() || it->is_null()) {
    return std::nullopt;
  }
  if (!it->is_array() || it->size() > std::numeric_limits<uint32_t>::max()) {
    malformed = true;
    return std::nullopt;
  }
  return static_cast<uint32_t>(it->size());
}

std::optional<uint64_t> readLimit(const nlohmann::json& json, const std::string& key,
                                  bool& malformed) {
  return readCount(json, key, malformed, NullPolicy::AllowNullAsAbsent);
}

// Top-level `model` and `stream`, which every known API shares.
void readCommon(const nlohmann::json& json, RequestAttributes& attrs) {
  if (auto model = readString(json, Keys::get().Model, attrs.malformed); model.has_value()) {
    attrs.model = std::move(model).value();
  }
  attrs.stream = readBool(json, Keys::get().Stream, attrs.malformed);
}

void readOpenAiChatCompletions(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::get().MaxCompletionTokens, attrs.malformed);
  if (!attrs.max_output_tokens.has_value()) {
    attrs.max_output_tokens = readLimit(json, Keys::get().MaxTokens, attrs.malformed);
  }
  attrs.message_count = readArrayLength(json, Keys::get().Messages, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::get().Tools, attrs.malformed);
}

void readOpenAiResponses(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::get().MaxOutputTokens, attrs.malformed);
  // A string `input`, inline or offloaded, is the shorthand for one user message.
  if (const auto input = json.find(Keys::get().Input);
      input != json.end() && (input->is_string() || JsonWithExtBuf::isExternalRef(*input))) {
    attrs.message_count = 1;
  } else {
    attrs.message_count = readArrayLength(json, Keys::get().Input, attrs.malformed);
  }
  attrs.tool_count = readArrayLength(json, Keys::get().Tools, attrs.malformed);
}

void readAnthropicMessages(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::get().MaxTokens, attrs.malformed);
  attrs.message_count = readArrayLength(json, Keys::get().Messages, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::get().Tools, attrs.malformed);
}

// `/{version}/models/{model}:generateContent` or `:streamGenerateContent`;
// Vertex nests the same final segment. Anything else leaves both unset.
void readGeminiTarget(absl::string_view path, RequestAttributes& attrs) {
  path = path.substr(0, path.find('?'));
  const size_t last_slash = path.rfind('/');
  if (last_slash == absl::string_view::npos ||
      !absl::EndsWith(path.substr(0, last_slash), "/models")) {
    return;
  }
  const absl::string_view segment = path.substr(last_slash + 1);
  const size_t colon = segment.find(':');
  if (colon == absl::string_view::npos) {
    return;
  }
  const absl::string_view model = segment.substr(0, colon);
  const absl::string_view operation = segment.substr(colon + 1);
  if (model.empty()) {
    return;
  }
  if (operation == "generateContent") {
    attrs.stream = false;
  } else if (operation == "streamGenerateContent") {
    attrs.stream = true;
  } else {
    return;
  }
  if (model.size() > MaxModelBytes) {
    attrs.malformed = true;
    return;
  }
  attrs.model = std::string(model);
}

void readGeminiGenerateContent(const nlohmann::json& json, absl::string_view path,
                               RequestAttributes& attrs) {
  readGeminiTarget(path, attrs);
  // Gemini's proto3 JSON accepts snake_case field names alongside camelCase.
  const nlohmann::json* config = readObject(json, Keys::get().GenerationConfig, attrs.malformed,
                                            NullPolicy::AllowNullAsAbsent);
  if (config == nullptr) {
    config = readObject(json, Keys::get().GenerationConfigSnake, attrs.malformed,
                        NullPolicy::AllowNullAsAbsent);
  }
  if (config != nullptr) {
    attrs.max_output_tokens = readLimit(*config, Keys::get().MaxOutputTokensCamel, attrs.malformed);
    if (!attrs.max_output_tokens.has_value()) {
      attrs.max_output_tokens = readLimit(*config, Keys::get().MaxOutputTokens, attrs.malformed);
    }
  }
  attrs.message_count = readArrayLength(json, Keys::get().Contents, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::get().Tools, attrs.malformed);
}

} // namespace

RequestAttributes extractRequestAttributes(ApiProtocol protocol, const nlohmann::json& json,
                                           absl::string_view path) {
  RequestAttributes attrs;
  attrs.api_protocol = protocol;
  switch (protocol) {
  case ApiProtocol::OpenAiChatCompletions:
    readOpenAiChatCompletions(json, attrs);
    break;
  case ApiProtocol::OpenAiResponses:
    readOpenAiResponses(json, attrs);
    break;
  case ApiProtocol::AnthropicMessages:
    readAnthropicMessages(json, attrs);
    break;
  case ApiProtocol::GeminiGenerateContent:
    readGeminiGenerateContent(json, path, attrs);
    break;
  case ApiProtocol::Unspecified:
    readCommon(json, attrs);
    break;
  }
  return attrs;
}

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
