#include "source/extensions/filters/ai/request_info/extractor.h"

#include <string>
#include <utility>

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
using HttpFilters::AiProtocolManager::MaxStringValueSize;
using HttpFilters::AiProtocolManager::NullPolicy;
using HttpFilters::AiProtocolManager::readArrayLength;
using HttpFilters::AiProtocolManager::readBool;
using HttpFilters::AiProtocolManager::readCount;
using HttpFilters::AiProtocolManager::readObject;
using HttpFilters::AiProtocolManager::readString;

namespace Keys = HttpFilters::AiProtocolManager::Keys;

namespace {

// These APIs document null as "unset", so a null limit is absent, not malformed.
std::optional<uint64_t> readLimit(const nlohmann::json& json, absl::string_view key,
                                  bool& malformed) {
  return readCount(json, key, malformed, NullPolicy::AllowNullAsAbsent);
}

void readCommon(const nlohmann::json& json, RequestAttributes& attrs) {
  if (auto model = readString(json, Keys::Model, attrs.malformed); model.has_value()) {
    attrs.model = std::move(model).value();
  }
  attrs.stream = readBool(json, Keys::Stream, attrs.malformed);
}

void readOpenAiChatCompletions(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::MaxCompletionTokens, attrs.malformed);
  if (!attrs.max_output_tokens.has_value()) {
    attrs.max_output_tokens = readLimit(json, Keys::MaxTokens, attrs.malformed);
  }
  attrs.message_count = readArrayLength(json, Keys::Messages, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::Tools, attrs.malformed);
}

void readOpenAiResponses(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::MaxOutputTokens, attrs.malformed);
  // A string `input`, inline or offloaded, is the shorthand for one user message.
  if (const auto input = json.find(Keys::Input);
      input != json.end() && (input->is_string() || JsonWithExtBuf::isExternalRef(*input))) {
    attrs.message_count = 1;
  } else {
    attrs.message_count = readArrayLength(json, Keys::Input, attrs.malformed);
  }
  attrs.tool_count = readArrayLength(json, Keys::Tools, attrs.malformed);
}

void readAnthropicMessages(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::MaxTokens, attrs.malformed);
  attrs.message_count = readArrayLength(json, Keys::Messages, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::Tools, attrs.malformed);
}

// `.../models/{model}:generateContent` or `:streamGenerateContent`, which Vertex nests too.
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
  if (model.size() > MaxStringValueSize) {
    attrs.malformed = true;
    return;
  }
  attrs.model = std::string(model);
}

void readGeminiGenerateContent(const nlohmann::json& json, absl::string_view path,
                               RequestAttributes& attrs) {
  readGeminiTarget(path, attrs);
  // Gemini's proto3 JSON accepts snake_case field names alongside camelCase.
  const nlohmann::json* config =
      readObject(json, Keys::GenerationConfig, attrs.malformed, NullPolicy::AllowNullAsAbsent);
  if (config == nullptr) {
    config = readObject(json, Keys::GenerationConfigSnake, attrs.malformed,
                        NullPolicy::AllowNullAsAbsent);
  }
  if (config != nullptr) {
    attrs.max_output_tokens = readLimit(*config, Keys::MaxOutputTokensCamel, attrs.malformed);
    if (!attrs.max_output_tokens.has_value()) {
      attrs.max_output_tokens = readLimit(*config, Keys::MaxOutputTokens, attrs.malformed);
    }
  }
  attrs.message_count = readArrayLength(json, Keys::Contents, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::Tools, attrs.malformed);
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
