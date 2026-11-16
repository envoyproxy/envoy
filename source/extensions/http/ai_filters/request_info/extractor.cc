#include "source/extensions/http/ai_filters/request_info/extractor.h"

#include <string>
#include <utility>

#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LlmBindingRead;
using HttpFilters::AiProtocolManager::LLMProtocol;
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

// What the path decided, over what the body says: the binding carries the model there alone, or
// the body named none. A model too long to be one reads as absent, as from the body.
void readArrived(const LlmBindingRead& arrived, RequestAttributes& attrs) {
  if (arrived.model_from_path) {
    if (arrived.model.size() > MaxStringValueSize) {
      attrs.malformed = true;
    } else {
      attrs.model = arrived.model;
    }
  }
  if (arrived.stream.has_value()) {
    attrs.stream = arrived.stream;
  }
}

void readGeminiGenerateContent(const nlohmann::json& json, RequestAttributes& attrs) {
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

RequestAttributes extractRequestAttributes(LLMProtocol protocol, const nlohmann::json& json,
                                           const LlmBindingRead* arrived) {
  RequestAttributes attrs;
  attrs.llm_protocol = protocol;
  switch (protocol) {
  case LLMProtocol::OpenAiChatCompletions:
    readOpenAiChatCompletions(json, attrs);
    break;
  case LLMProtocol::OpenAiResponses:
    readOpenAiResponses(json, attrs);
    break;
  case LLMProtocol::AnthropicMessages:
    readAnthropicMessages(json, attrs);
    break;
  case LLMProtocol::GeminiGenerateContent:
    readGeminiGenerateContent(json, attrs);
    break;
  case LLMProtocol::Unspecified:
    readCommon(json, attrs);
    break;
  }
  if (arrived != nullptr) {
    readArrived(*arrived, attrs);
  }
  return attrs;
}

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
