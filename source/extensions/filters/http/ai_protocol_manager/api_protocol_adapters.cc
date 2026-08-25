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

// The Unspecified protocol: no schema, no usage, no terminal events. Keeping
// it a real adapter makes AdapterRegistry::get() total, so callers never
// null-check.
class NullAdapter : public ApiProtocolAdapter {
public:
  ApiProtocol protocol() const override { return ApiProtocol::Unspecified; }
  const PayloadSchema* schema() const override { return nullptr; }
  void canonicalizeUsage(TokenUsage&, bool&) const override {}
  bool isTerminalEvent(const nlohmann::json&) const override { return false; }

protected:
  void extractUsageInto(const nlohmann::json&, ExtractionResult&) const override {}
};

// OpenAI: Chat Completions and Responses API. The two dialects share one
// usage structure with renamed keys; they never mix in one document, so
// reading either name from the same usage object is unambiguous. Responses
// API streaming lifecycle events nest the payload under `response`.
class OpenAiAdapterBase : public ApiProtocolAdapter {
public:
  // OpenAI's native counts are already inclusive.
  void canonicalizeUsage(TokenUsage&, bool&) const override {}

protected:
  void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const override {
    bool& malformed = result.malformed;
    TokenUsage& usage = result.usage;
    const nlohmann::json* response = readObject(json, JsonKeys::get().Response, malformed);
    const nlohmann::json& node = response != nullptr ? *response : json;

    if (auto model = readString(node, JsonKeys::get().Model); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node =
        readObject(node, JsonKeys::get().Usage, malformed, NullPolicy::AllowNullAsAbsent);
    if (usage_node == nullptr) {
      return;
    }

    usage.input_tokens = readCount(*usage_node, JsonKeys::get().PromptTokens, malformed);
    if (!usage.input_tokens.has_value()) {
      usage.input_tokens = readCount(*usage_node, JsonKeys::get().InputTokens, malformed);
    }
    usage.output_tokens = readCount(*usage_node, JsonKeys::get().CompletionTokens, malformed);
    if (!usage.output_tokens.has_value()) {
      usage.output_tokens = readCount(*usage_node, JsonKeys::get().OutputTokens, malformed);
    }
    usage.total_tokens = readCount(*usage_node, JsonKeys::get().TotalTokens, malformed);

    const nlohmann::json* input_details = readObject(
        *usage_node, JsonKeys::get().PromptTokensDetails, malformed, NullPolicy::AllowNullAsAbsent);
    if (input_details == nullptr) {
      input_details = readObject(*usage_node, JsonKeys::get().InputTokensDetails, malformed,
                                 NullPolicy::AllowNullAsAbsent);
    }
    if (input_details != nullptr) {
      usage.cached_input_tokens =
          readCount(*input_details, JsonKeys::get().CachedTokens, malformed);
      usage.cache_creation_input_tokens =
          readCount(*input_details, JsonKeys::get().CacheWriteTokens, malformed);
    }

    const nlohmann::json* output_details =
        readObject(*usage_node, JsonKeys::get().CompletionTokensDetails, malformed,
                   NullPolicy::AllowNullAsAbsent);
    if (output_details == nullptr) {
      output_details = readObject(*usage_node, JsonKeys::get().OutputTokensDetails, malformed,
                                  NullPolicy::AllowNullAsAbsent);
    }
    if (output_details != nullptr) {
      usage.reasoning_tokens =
          readCount(*output_details, JsonKeys::get().ReasoningTokens, malformed);
    }
  }
};

class OpenAiChatCompletionsAdapter : public OpenAiAdapterBase {
public:
  ApiProtocol protocol() const override { return ApiProtocol::OpenAiChatCompletions; }
  const PayloadSchema* schema() const override {
    // Construct-on-first-use: schemas are non-trivially destructible, so a
    // plain function-local static would register an exit-time destructor.
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
    // Terminal lifecycle events; also the usage carriers, so callers
    // extractUsage() first.
    const auto type = readString(json, JsonKeys::get().Type);
    return type.has_value() && isOpenAiResponsesTerminalEventType(type.value());
  }
};

// Anthropic Messages API. Non-streaming responses and `message_delta` events
// carry `usage` at the root; `message_start` nests a Message object (with
// `model` and the input-side usage) under `message`. `message_delta` counts
// are cumulative, which the caller's last-wins merge handles.
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
    const auto type = readString(json, JsonKeys::get().Type);
    return type.has_value() && type.value() == "message_stop";
  }

protected:
  void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const override {
    bool& malformed = result.malformed;
    TokenUsage& usage = result.usage;
    // Anthropic documents `event: error` after a 200 has streamed: the
    // terminal usage update never arrives, so the accumulation so far must
    // not publish as complete.
    if (const auto type = readString(json, JsonKeys::get().Type);
        type.has_value() && type.value() == "error") {
      result.stream_error = true;
      return;
    }
    const nlohmann::json* message = readObject(json, JsonKeys::get().Message, malformed);
    const nlohmann::json& node = message != nullptr ? *message : json;

    if (auto model = readString(node, JsonKeys::get().Model); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node = readObject(node, JsonKeys::get().Usage, malformed);
    if (usage_node == nullptr) {
      return;
    }

    // Native counts only: the disjoint input/cache buckets are summed once,
    // in canonicalizeUsage() -- summing per event would let a partial update
    // regress the accumulated value via last-wins merge.
    usage.input_tokens = readCount(*usage_node, JsonKeys::get().InputTokens, malformed);
    // `output_tokens` already includes thinking tokens (inclusive).
    usage.output_tokens = readCount(*usage_node, JsonKeys::get().OutputTokens, malformed);
    // No total_tokens in this dialect; computed at finalize.
    usage.cached_input_tokens =
        readCount(*usage_node, JsonKeys::get().CacheReadInputTokens, malformed);
    usage.cache_creation_input_tokens =
        readCount(*usage_node, JsonKeys::get().CacheCreationInputTokens, malformed);

    if (const nlohmann::json* details = readObject(*usage_node, JsonKeys::get().OutputTokensDetails,
                                                   malformed, NullPolicy::AllowNullAsAbsent);
        details != nullptr) {
      usage.reasoning_tokens = readCount(*details, JsonKeys::get().ThinkingTokens, malformed);
    }
  }
};

// Gemini generateContent / streamGenerateContent. Every chunk is a
// GenerateContentResponse; `usageMetadata` snapshots are cumulative (last
// wins). `cachedContentTokenCount` is a subset of `promptTokenCount`, so it
// maps to cached_input_tokens without any arithmetic.
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
    if (auto model = readString(json, JsonKeys::get().ModelVersion); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node = readObject(json, JsonKeys::get().UsageMetadata, malformed);
    if (usage_node == nullptr) {
      return;
    }

    // Native counts only; the tool-use and thoughts adjuncts are summed in at
    // canonicalizeUsage(), after the last cumulative snapshot merged.
    usage.input_tokens = readCount(*usage_node, JsonKeys::get().PromptTokenCount, malformed);
    usage.output_tokens = readCount(*usage_node, JsonKeys::get().CandidatesTokenCount, malformed);
    usage.total_tokens = readCount(*usage_node, JsonKeys::get().TotalTokenCount, malformed);
    usage.cached_input_tokens =
        readCount(*usage_node, JsonKeys::get().CachedContentTokenCount, malformed);
    usage.tool_use_input_tokens =
        readCount(*usage_node, JsonKeys::get().ToolUsePromptTokenCount, malformed);
    usage.reasoning_tokens = readCount(*usage_node, JsonKeys::get().ThoughtsTokenCount, malformed);
  }
};

} // namespace

const ApiProtocolAdapter& AdapterRegistry::get(ApiProtocol protocol) {
  // Construct-on-first-use: adapters have virtual destructors, so plain
  // function-local statics would register exit-time destructors.
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

// Detection stays centralized rather than delegated per adapter: the marker
// checks are ordered from most to least structurally distinctive across
// dialects, and that cross-adapter ordering is part of the detection
// contract.
ApiProtocol AdapterRegistry::detect(const nlohmann::json& json) {
  // Gemini markers, validated by value shape: a foreign document with e.g. a
  // `candidates` *string* must not lock the stream. Real candidates lists are
  // non-empty arrays of objects.
  if (const auto it = json.find(JsonKeys::get().Candidates);
      it != json.end() && it->is_array() && !it->empty() && it->front().is_object()) {
    return ApiProtocol::GeminiGenerateContent;
  }
  if (const auto it = json.find(JsonKeys::get().UsageMetadata);
      it != json.end() && it->is_object()) {
    return ApiProtocol::GeminiGenerateContent;
  }
  if (readString(json, JsonKeys::get().ModelVersion).has_value()) {
    return ApiProtocol::GeminiGenerateContent;
  }

  // OpenAI Chat Completions and non-streaming Responses discriminate on
  // `object`; Responses streaming events discriminate on `type` ("response.*").
  if (const auto object = readString(json, JsonKeys::get().ObjectKey); object.has_value()) {
    if (absl::StartsWith(object.value(), "chat.completion")) {
      return ApiProtocol::OpenAiChatCompletions;
    }
    if (object.value() == "response") {
      return ApiProtocol::OpenAiResponses;
    }
  }

  if (const auto type = readString(json, JsonKeys::get().Type); type.has_value()) {
    const absl::string_view type_view = type.value();
    if (absl::StartsWith(type_view, "response.")) {
      return ApiProtocol::OpenAiResponses;
    }
    // Anthropic markers need their documented companion structure: bare
    // `type` strings are generic, and a genuine stream always presents
    // message_start (nested Message) or a non-streaming Message (role/usage)
    // before any usage, so skipping the bare event types loses nothing.
    bool discard = false;
    if (type_view == "message") {
      if (readString(json, JsonKeys::get().Role).has_value() ||
          readObject(json, JsonKeys::get().Usage, discard) != nullptr) {
        return ApiProtocol::AnthropicMessages;
      }
    } else if (type_view == "message_start") {
      if (readObject(json, JsonKeys::get().Message, discard) != nullptr) {
        return ApiProtocol::AnthropicMessages;
      }
    } else if (type_view == "message_delta") {
      if (readObject(json, JsonKeys::get().Usage, discard) != nullptr ||
          readObject(json, JsonKeys::get().Delta, discard) != nullptr) {
        return ApiProtocol::AnthropicMessages;
      }
    }
    // `message_stop`/`content_block_*` carry no structure and no usage.
  }

  return ApiProtocol::Unspecified;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
