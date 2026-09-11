#include <utility>

#include "source/common/common/macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/api_protocol_adapter.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/openai_chat_completions.h"

#include "absl/strings/match.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// Keeps AdapterRegistry::get() total, so callers never null-check.
class NullAdapter : public ApiProtocolAdapter {
public:
  ApiProtocol protocol() const override { return ApiProtocol::Unspecified; }
  const PayloadSchema* schema() const override { return nullptr; }
  void canonicalizeUsage(TokenUsage&, bool&) const override {}
  bool isTerminalEvent(const nlohmann::json&) const override { return false; }

protected:
  void extractUsageInto(const nlohmann::json&, ExtractionResult&) const override {}
};

// Chat Completions and Responses share one usage shape under different key names; Responses
// streaming events nest the payload under `response`.
class OpenAiAdapterBase : public ApiProtocolAdapter {
public:
  // OpenAI's native counts are already inclusive.
  void canonicalizeUsage(TokenUsage&, bool&) const override {}

protected:
  void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const override {
    bool& malformed = result.malformed;
    TokenUsage& usage = result.usage;
    const nlohmann::json* response = readObject(json, "response", malformed);
    const nlohmann::json& node = response != nullptr ? *response : json;

    if (auto model = readString(node, "model"); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node =
        readObject(node, "usage", malformed, NullPolicy::AllowNullAsAbsent);
    if (usage_node == nullptr) {
      return;
    }

    usage.input_tokens = readCount(*usage_node, "prompt_tokens", malformed);
    if (!usage.input_tokens.has_value()) {
      usage.input_tokens = readCount(*usage_node, "input_tokens", malformed);
    }
    usage.output_tokens = readCount(*usage_node, "completion_tokens", malformed);
    if (!usage.output_tokens.has_value()) {
      usage.output_tokens = readCount(*usage_node, "output_tokens", malformed);
    }
    usage.total_tokens = readCount(*usage_node, "total_tokens", malformed);

    const nlohmann::json* input_details =
        readObject(*usage_node, "prompt_tokens_details", malformed, NullPolicy::AllowNullAsAbsent);
    if (input_details == nullptr) {
      input_details =
          readObject(*usage_node, "input_tokens_details", malformed, NullPolicy::AllowNullAsAbsent);
    }
    if (input_details != nullptr) {
      usage.cached_input_tokens = readCount(*input_details, "cached_tokens", malformed);
      usage.cache_creation_input_tokens =
          readCount(*input_details, "cache_write_tokens", malformed);
    }

    const nlohmann::json* output_details = readObject(*usage_node, "completion_tokens_details",
                                                      malformed, NullPolicy::AllowNullAsAbsent);
    if (output_details == nullptr) {
      output_details = readObject(*usage_node, "output_tokens_details", malformed,
                                  NullPolicy::AllowNullAsAbsent);
    }
    if (output_details != nullptr) {
      usage.reasoning_tokens = readCount(*output_details, "reasoning_tokens", malformed);
    }
  }
};

class OpenAiChatCompletionsAdapter : public OpenAiAdapterBase {
public:
  ApiProtocol protocol() const override { return ApiProtocol::OpenAiChatCompletions; }
  const PayloadSchema* schema() const override {
    // Leaked so no exit-time destructor is registered.
    static const PayloadSchema* openai_schema = new PayloadSchema(OpenAI::createPayloadSchema());
    return openai_schema;
  }
  // Terminates with the non-JSON `[DONE]` sentinel, handled before parsing.
  bool isTerminalEvent(const nlohmann::json&) const override { return false; }
};

class OpenAiResponsesAdapter : public OpenAiAdapterBase {
public:
  ApiProtocol protocol() const override { return ApiProtocol::OpenAiResponses; }
  const PayloadSchema* schema() const override { return nullptr; }
  bool isTerminalEvent(const nlohmann::json& json) const override {
    // These events also carry the usage, so callers extractUsage() first.
    const auto type = readString(json, "type");
    return type.has_value() && isOpenAiResponsesTerminalEventType(type.value());
  }
};

// `message_start` nests a Message under `message`; `message_delta` usage is cumulative.
class AnthropicMessagesAdapter : public ApiProtocolAdapter {
public:
  ApiProtocol protocol() const override { return ApiProtocol::AnthropicMessages; }
  const PayloadSchema* schema() const override { return nullptr; }

  void canonicalizeUsage(TokenUsage& usage, bool& overflow) const override {
    // Native input excludes the two disjoint cache buckets.
    if (usage.input_tokens.has_value()) {
      usage.input_tokens =
          addCounts(addCounts(usage.input_tokens, usage.cached_input_tokens, overflow),
                    usage.cache_creation_input_tokens, overflow);
    }
  }

  bool isTerminalEvent(const nlohmann::json& json) const override {
    const auto type = readString(json, "type");
    return type.has_value() && type.value() == "message_stop";
  }

protected:
  void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const override {
    bool& malformed = result.malformed;
    TokenUsage& usage = result.usage;
    // Anthropic can send `event: error` after a 200; the terminal usage then never arrives.
    if (const auto type = readString(json, "type"); type.has_value() && type.value() == "error") {
      result.stream_error = true;
      return;
    }
    const nlohmann::json* message = readObject(json, "message", malformed);
    const nlohmann::json& node = message != nullptr ? *message : json;

    if (auto model = readString(node, "model"); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node = readObject(node, "usage", malformed);
    if (usage_node == nullptr) {
      return;
    }

    // Native counts only: summing per event would let a partial update regress last-wins merge.
    usage.input_tokens = readCount(*usage_node, "input_tokens", malformed);
    // `output_tokens` already includes thinking tokens.
    usage.output_tokens = readCount(*usage_node, "output_tokens", malformed);
    usage.cached_input_tokens = readCount(*usage_node, "cache_read_input_tokens", malformed);
    usage.cache_creation_input_tokens =
        readCount(*usage_node, "cache_creation_input_tokens", malformed);

    if (const nlohmann::json* details = readObject(*usage_node, "output_tokens_details", malformed,
                                                   NullPolicy::AllowNullAsAbsent);
        details != nullptr) {
      usage.reasoning_tokens = readCount(*details, "thinking_tokens", malformed);
    }
  }
};

// `usageMetadata` snapshots are cumulative; `cachedContentTokenCount` is a subset of
// `promptTokenCount`.
class GeminiGenerateContentAdapter : public ApiProtocolAdapter {
public:
  ApiProtocol protocol() const override { return ApiProtocol::GeminiGenerateContent; }
  const PayloadSchema* schema() const override { return nullptr; }

  void canonicalizeUsage(TokenUsage& usage, bool& overflow) const override {
    // Native prompt/candidates counts exclude tool-use and thoughts.
    if (usage.input_tokens.has_value()) {
      usage.input_tokens = addCounts(usage.input_tokens, usage.tool_use_input_tokens, overflow);
    }
    if (usage.output_tokens.has_value()) {
      usage.output_tokens = addCounts(usage.output_tokens, usage.reasoning_tokens, overflow);
    }
  }

  // No in-band terminator; extraction finalizes at end of stream.
  bool isTerminalEvent(const nlohmann::json&) const override { return false; }

protected:
  void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const override {
    bool& malformed = result.malformed;
    TokenUsage& usage = result.usage;
    if (auto model = readString(json, "modelVersion"); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node = readObject(json, "usageMetadata", malformed);
    if (usage_node == nullptr) {
      return;
    }

    // Native counts only; the adjuncts are summed in canonicalizeUsage(), after the last
    // cumulative snapshot merges.
    usage.input_tokens = readCount(*usage_node, "promptTokenCount", malformed);
    usage.output_tokens = readCount(*usage_node, "candidatesTokenCount", malformed);
    usage.total_tokens = readCount(*usage_node, "totalTokenCount", malformed);
    usage.cached_input_tokens = readCount(*usage_node, "cachedContentTokenCount", malformed);
    usage.tool_use_input_tokens = readCount(*usage_node, "toolUsePromptTokenCount", malformed);
    usage.reasoning_tokens = readCount(*usage_node, "thoughtsTokenCount", malformed);
  }
};

} // namespace

const ApiProtocolAdapter& AdapterRegistry::get(ApiProtocol protocol) {
  switch (protocol) {
  case ApiProtocol::OpenAiChatCompletions:
    CONSTRUCT_ON_FIRST_USE(OpenAiChatCompletionsAdapter);
  case ApiProtocol::OpenAiResponses:
    CONSTRUCT_ON_FIRST_USE(OpenAiResponsesAdapter);
  case ApiProtocol::AnthropicMessages:
    CONSTRUCT_ON_FIRST_USE(AnthropicMessagesAdapter);
  case ApiProtocol::GeminiGenerateContent:
    CONSTRUCT_ON_FIRST_USE(GeminiGenerateContentAdapter);
  case ApiProtocol::Unspecified:
    break;
  }
  CONSTRUCT_ON_FIRST_USE(NullAdapter);
}

void finalizeUsage(TokenUsage& usage) { usage.finalize(AdapterRegistry::get(usage.api_protocol)); }

bool isOpenAiResponsesTerminalEventType(absl::string_view event_type) {
  return event_type == "response.completed" || event_type == "response.failed" ||
         event_type == "response.incomplete";
}

// Not delegated per adapter: markers are checked from most to least distinctive across dialects.
ApiProtocol AdapterRegistry::detect(const nlohmann::json& json) {
  // Checked by value shape so a foreign `candidates` string cannot lock the stream.
  if (const auto it = json.find("candidates");
      it != json.end() && it->is_array() && !it->empty() && it->front().is_object()) {
    return ApiProtocol::GeminiGenerateContent;
  }
  if (const auto it = json.find("usageMetadata"); it != json.end() && it->is_object()) {
    return ApiProtocol::GeminiGenerateContent;
  }
  if (readString(json, "modelVersion").has_value()) {
    return ApiProtocol::GeminiGenerateContent;
  }

  if (const auto object = readString(json, "object"); object.has_value()) {
    if (absl::StartsWith(object.value(), "chat.completion")) {
      return ApiProtocol::OpenAiChatCompletions;
    }
    if (object.value() == "response") {
      return ApiProtocol::OpenAiResponses;
    }
  }

  if (const auto type = readString(json, "type"); type.has_value()) {
    const absl::string_view type_view = type.value();
    if (absl::StartsWith(type_view, "response.")) {
      return ApiProtocol::OpenAiResponses;
    }
    // Bare `type` strings are generic, so require companion structure; a real stream presents
    // message_start or a full Message before any usage, so skipping bare events loses nothing.
    bool discard = false;
    if (type_view == "message") {
      if (readString(json, "role").has_value() || readObject(json, "usage", discard) != nullptr) {
        return ApiProtocol::AnthropicMessages;
      }
    } else if (type_view == "message_start") {
      if (readObject(json, "message", discard) != nullptr) {
        return ApiProtocol::AnthropicMessages;
      }
    } else if (type_view == "message_delta") {
      if (readObject(json, "usage", discard) != nullptr ||
          readObject(json, "delta", discard) != nullptr) {
        return ApiProtocol::AnthropicMessages;
      }
    }
  }

  return ApiProtocol::Unspecified;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
