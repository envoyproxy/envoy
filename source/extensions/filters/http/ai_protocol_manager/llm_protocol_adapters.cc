#include <optional>
#include <string>
#include <utility>

#include "source/common/common/macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_adapter.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/anthropic_messages.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/gemini_generate_content.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/openai_chat_completions.h"

#include "absl/strings/match.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// renderUsage() helpers: write only what the canonical usage carries.
void putCount(nlohmann::json& out, absl::string_view key, const std::optional<uint64_t>& count) {
  if (count.has_value()) {
    out[std::string(key)] = count.value();
  }
}

void putObject(nlohmann::json& out, absl::string_view key, nlohmann::json object) {
  if (!object.empty()) {
    out[std::string(key)] = std::move(object);
  }
}

// A canonical (inclusive) count minus a breakdown bucket that the dialect
// reports beside it rather than inside it. Saturates at zero, so an
// inconsistent provider report cannot wrap around.
std::optional<uint64_t> excludeBucket(const std::optional<uint64_t>& count,
                                      const std::optional<uint64_t>& bucket) {
  if (!count.has_value()) {
    return std::nullopt;
  }
  const uint64_t excluded = bucket.value_or(0);
  return count.value() > excluded ? count.value() - excluded : 0;
}

// The canonical total when both components are known, else the provider's.
std::optional<uint64_t> totalForRender(const TokenUsage& usage) {
  return usage.total_tokens.has_value() ? usage.total_tokens : usage.provider_total_tokens;
}

// The Unspecified protocol: no schema, no usage, no terminal events. Keeping
// it a real adapter makes AdapterRegistry::get() total, so callers never
// null-check.
class NullAdapter : public LLMProtocolAdapter {
public:
  LLMProtocol protocol() const override { return LLMProtocol::Unspecified; }
  const PayloadSchema* schema() const override { return nullptr; }
  void canonicalizeUsage(TokenUsage&, bool&) const override {}
  bool isTerminalEvent(const nlohmann::json&) const override { return false; }
  absl::string_view usagePath() const override { return ""; }
  nlohmann::json renderUsage(const TokenUsage&) const override { return nlohmann::json::object(); }

protected:
  void extractUsageInto(const nlohmann::json&, ExtractionResult&) const override {}
};

// OpenAI: Chat Completions and Responses API. The two dialects share one
// usage structure with renamed keys; they never mix in one document, so
// reading either name from the same usage object is unambiguous. Responses
// API streaming lifecycle events nest the payload under `response`.
class OpenAiAdapterBase : public LLMProtocolAdapter {
public:
  // OpenAI's native counts are already inclusive.
  void canonicalizeUsage(TokenUsage&, bool&) const override {}
  absl::string_view usagePath() const override { return Keys::Usage; }

protected:
  // The names one of the two dialects gives the shared usage structure.
  struct UsageKeys {
    absl::string_view input;
    absl::string_view output;
    absl::string_view input_details;
    absl::string_view output_details;
  };

  // Native counts are canonical already, so rendering is a rename.
  static nlohmann::json renderUsageAs(const TokenUsage& usage, const UsageKeys& keys) {
    nlohmann::json out = nlohmann::json::object();
    putCount(out, keys.input, usage.input_tokens);
    putCount(out, keys.output, usage.output_tokens);
    putCount(out, Keys::TotalTokens, totalForRender(usage));
    nlohmann::json input_details = nlohmann::json::object();
    putCount(input_details, Keys::CachedTokens, usage.cached_input_tokens);
    putCount(input_details, Keys::CacheWriteTokens, usage.cache_creation_input_tokens);
    putObject(out, keys.input_details, std::move(input_details));
    nlohmann::json output_details = nlohmann::json::object();
    putCount(output_details, Keys::ReasoningTokens, usage.reasoning_tokens);
    putObject(out, keys.output_details, std::move(output_details));
    return out;
  }

  void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const override {
    bool& malformed = result.malformed;
    TokenUsage& usage = result.usage;
    const nlohmann::json* response = readObject(json, Keys::Response, malformed);
    const nlohmann::json& node = response != nullptr ? *response : json;

    if (auto model = readString(node, Keys::Model); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node =
        readObject(node, Keys::Usage, malformed, NullPolicy::AllowNullAsAbsent);
    if (usage_node == nullptr) {
      return;
    }

    usage.input_tokens = readCount(*usage_node, Keys::PromptTokens, malformed);
    if (!usage.input_tokens.has_value()) {
      usage.input_tokens = readCount(*usage_node, Keys::InputTokens, malformed);
    }
    usage.output_tokens = readCount(*usage_node, Keys::CompletionTokens, malformed);
    if (!usage.output_tokens.has_value()) {
      usage.output_tokens = readCount(*usage_node, Keys::OutputTokens, malformed);
    }
    usage.total_tokens = readCount(*usage_node, Keys::TotalTokens, malformed);

    const nlohmann::json* input_details = readObject(*usage_node, Keys::PromptTokensDetails,
                                                     malformed, NullPolicy::AllowNullAsAbsent);
    if (input_details == nullptr) {
      input_details = readObject(*usage_node, Keys::InputTokensDetails, malformed,
                                 NullPolicy::AllowNullAsAbsent);
    }
    if (input_details != nullptr) {
      usage.cached_input_tokens = readCount(*input_details, Keys::CachedTokens, malformed);
      usage.cache_creation_input_tokens =
          readCount(*input_details, Keys::CacheWriteTokens, malformed);
    }

    const nlohmann::json* output_details = readObject(*usage_node, Keys::CompletionTokensDetails,
                                                      malformed, NullPolicy::AllowNullAsAbsent);
    if (output_details == nullptr) {
      output_details = readObject(*usage_node, Keys::OutputTokensDetails, malformed,
                                  NullPolicy::AllowNullAsAbsent);
    }
    if (output_details != nullptr) {
      usage.reasoning_tokens = readCount(*output_details, Keys::ReasoningTokens, malformed);
    }
  }
};

class OpenAiChatCompletionsAdapter : public OpenAiAdapterBase {
public:
  LLMProtocol protocol() const override { return LLMProtocol::OpenAiChatCompletions; }
  const PayloadSchema* schema() const override {
    // Construct-on-first-use: schemas are non-trivially destructible, so a
    // plain function-local static would register an exit-time destructor.
    static const PayloadSchema* openai_schema = new PayloadSchema(OpenAI::createPayloadSchema());
    return openai_schema;
  }
  // Terminates with the non-JSON `[DONE]` sentinel, handled before parsing.
  bool isTerminalEvent(const nlohmann::json&) const override { return false; }
  nlohmann::json renderUsage(const TokenUsage& usage) const override {
    return renderUsageAs(usage, {Keys::PromptTokens, Keys::CompletionTokens,
                                 Keys::PromptTokensDetails, Keys::CompletionTokensDetails});
  }
};

class OpenAiResponsesAdapter : public OpenAiAdapterBase {
public:
  LLMProtocol protocol() const override { return LLMProtocol::OpenAiResponses; }
  const PayloadSchema* schema() const override { return nullptr; }
  bool isTerminalEvent(const nlohmann::json& json) const override {
    // Terminal lifecycle events; also the usage carriers, so callers
    // extractUsage() first.
    const auto type = readString(json, Keys::Type);
    return type.has_value() && isOpenAiResponsesTerminalEventType(type.value());
  }
  nlohmann::json renderUsage(const TokenUsage& usage) const override {
    return renderUsageAs(usage, {Keys::InputTokens, Keys::OutputTokens, Keys::InputTokensDetails,
                                 Keys::OutputTokensDetails});
  }
};

// Anthropic Messages API. Non-streaming responses and `message_delta` events
// carry `usage` at the root; `message_start` nests a Message object (with
// `model` and the input-side usage) under `message`. `message_delta` counts
// are cumulative, which the caller's last-wins merge handles.
class AnthropicMessagesAdapter : public LLMProtocolAdapter {
public:
  LLMProtocol protocol() const override { return LLMProtocol::AnthropicMessages; }
  const PayloadSchema* schema() const override {
    static const PayloadSchema* anthropic_schema =
        new PayloadSchema(Anthropic::createPayloadSchema());
    return anthropic_schema;
  }

  void canonicalizeUsage(TokenUsage& usage, bool& overflow) const override {
    // Native input excludes the two disjoint cache buckets.
    usage.input_tokens =
        addCounts(addCounts(usage.input_tokens, usage.cached_input_tokens, overflow),
                  usage.cache_creation_input_tokens, overflow);
  }

  bool isTerminalEvent(const nlohmann::json& json) const override {
    const auto type = readString(json, Keys::Type);
    return type.has_value() && type.value() == "message_stop";
  }

  absl::string_view usagePath() const override { return Keys::Usage; }

  // The inverse of canonicalizeUsage(): native input excludes both cache
  // buckets, which are reported beside it. Reasoning is not rendered: the
  // documented usage object has no field for it, and `output_tokens` already
  // includes it.
  nlohmann::json renderUsage(const TokenUsage& usage) const override {
    nlohmann::json out = nlohmann::json::object();
    putCount(out, Keys::InputTokens,
             excludeBucket(excludeBucket(usage.input_tokens, usage.cached_input_tokens),
                           usage.cache_creation_input_tokens));
    putCount(out, Keys::OutputTokens, usage.output_tokens);
    putCount(out, Keys::CacheReadInputTokens, usage.cached_input_tokens);
    putCount(out, Keys::CacheCreationInputTokens, usage.cache_creation_input_tokens);
    return out;
  }

protected:
  void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const override {
    bool& malformed = result.malformed;
    TokenUsage& usage = result.usage;
    // Anthropic documents `event: error` after a 200 has streamed: the
    // terminal usage update never arrives, so the accumulation so far must
    // not publish as complete.
    if (const auto type = readString(json, Keys::Type);
        type.has_value() && type.value() == "error") {
      result.stream_error = true;
      return;
    }
    const nlohmann::json* message = readObject(json, Keys::Message, malformed);
    const nlohmann::json& node = message != nullptr ? *message : json;

    if (auto model = readString(node, Keys::Model); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node = readObject(node, Keys::Usage, malformed);
    if (usage_node == nullptr) {
      return;
    }

    // Native counts only: the disjoint input/cache buckets are summed once,
    // in canonicalizeUsage() -- summing per event would let a partial update
    // regress the accumulated value via last-wins merge.
    usage.input_tokens = readCount(*usage_node, Keys::InputTokens, malformed);
    // `output_tokens` already includes thinking tokens (inclusive).
    usage.output_tokens = readCount(*usage_node, Keys::OutputTokens, malformed);
    // No total_tokens in this dialect; computed at finalize.
    usage.cached_input_tokens = readCount(*usage_node, Keys::CacheReadInputTokens, malformed);
    usage.cache_creation_input_tokens =
        readCount(*usage_node, Keys::CacheCreationInputTokens, malformed);

    if (const nlohmann::json* details = readObject(*usage_node, Keys::OutputTokensDetails,
                                                   malformed, NullPolicy::AllowNullAsAbsent);
        details != nullptr) {
      usage.reasoning_tokens = readCount(*details, Keys::ThinkingTokens, malformed);
    }
  }
};

// Gemini generateContent / streamGenerateContent. Every chunk is a
// GenerateContentResponse; `usageMetadata` snapshots are cumulative (last
// wins). `cachedContentTokenCount` is a subset of `promptTokenCount`, so it
// maps to cached_input_tokens without any arithmetic.
class GeminiGenerateContentAdapter : public LLMProtocolAdapter {
public:
  LLMProtocol protocol() const override { return LLMProtocol::GeminiGenerateContent; }
  const PayloadSchema* schema() const override {
    // Construct-on-first-use, as for Chat Completions above.
    static const PayloadSchema* gemini_schema = new PayloadSchema(Gemini::createPayloadSchema());
    return gemini_schema;
  }

  void canonicalizeUsage(TokenUsage& usage, bool& overflow) const override {
    // Native prompt/candidates counts exclude tool-use and thoughts, and are
    // themselves absent when the model produced only the adjunct: a response
    // truncated at MAX_TOKENS while still thinking reports
    // `thoughtsTokenCount` with no `candidatesTokenCount` at all.
    usage.input_tokens = addCounts(usage.input_tokens, usage.tool_use_input_tokens, overflow);
    usage.output_tokens = addCounts(usage.output_tokens, usage.reasoning_tokens, overflow);
  }

  // No in-band terminator; extraction finalizes at end of stream.
  bool isTerminalEvent(const nlohmann::json&) const override { return false; }

  absl::string_view usagePath() const override { return Keys::UsageMetadata; }

  // The inverse of canonicalizeUsage(): native prompt/candidates counts
  // exclude the tool-use and thoughts adjuncts, which are reported beside
  // them.
  nlohmann::json renderUsage(const TokenUsage& usage) const override {
    nlohmann::json out = nlohmann::json::object();
    putCount(out, Keys::PromptTokenCount,
             excludeBucket(usage.input_tokens, usage.tool_use_input_tokens));
    putCount(out, Keys::CandidatesTokenCount,
             excludeBucket(usage.output_tokens, usage.reasoning_tokens));
    putCount(out, Keys::TotalTokenCount, totalForRender(usage));
    putCount(out, Keys::CachedContentTokenCount, usage.cached_input_tokens);
    putCount(out, Keys::ToolUsePromptTokenCount, usage.tool_use_input_tokens);
    putCount(out, Keys::ThoughtsTokenCount, usage.reasoning_tokens);
    return out;
  }

protected:
  void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const override {
    bool& malformed = result.malformed;
    TokenUsage& usage = result.usage;
    if (auto model = readString(json, Keys::ModelVersion); model.has_value()) {
      usage.model = std::move(model).value();
    }

    const nlohmann::json* usage_node = readObject(json, Keys::UsageMetadata, malformed);
    if (usage_node == nullptr) {
      return;
    }

    // Native counts only; the tool-use and thoughts adjuncts are summed in at
    // canonicalizeUsage(), after the last cumulative snapshot merged.
    usage.input_tokens = readCount(*usage_node, Keys::PromptTokenCount, malformed);
    usage.output_tokens = readCount(*usage_node, Keys::CandidatesTokenCount, malformed);
    usage.total_tokens = readCount(*usage_node, Keys::TotalTokenCount, malformed);
    usage.cached_input_tokens = readCount(*usage_node, Keys::CachedContentTokenCount, malformed);
    usage.tool_use_input_tokens = readCount(*usage_node, Keys::ToolUsePromptTokenCount, malformed);
    usage.reasoning_tokens = readCount(*usage_node, Keys::ThoughtsTokenCount, malformed);
  }
};

} // namespace

const LLMProtocolAdapter& AdapterRegistry::get(LLMProtocol protocol) {
  // Construct-on-first-use: adapters have virtual destructors, so plain
  // function-local statics would register exit-time destructors.
  switch (protocol) {
  case LLMProtocol::OpenAiChatCompletions:
    CONSTRUCT_ON_FIRST_USE(OpenAiChatCompletionsAdapter);
  case LLMProtocol::OpenAiResponses:
    CONSTRUCT_ON_FIRST_USE(OpenAiResponsesAdapter);
  case LLMProtocol::AnthropicMessages:
    CONSTRUCT_ON_FIRST_USE(AnthropicMessagesAdapter);
  case LLMProtocol::GeminiGenerateContent:
    CONSTRUCT_ON_FIRST_USE(GeminiGenerateContentAdapter);
  case LLMProtocol::Unspecified:
    break;
  }
  CONSTRUCT_ON_FIRST_USE(NullAdapter);
}

void finalizeUsage(TokenUsage& usage) { usage.finalize(AdapterRegistry::get(usage.llm_protocol)); }

bool isOpenAiResponsesTerminalEventType(absl::string_view event_type) {
  return event_type == "response.completed" || event_type == "response.failed" ||
         event_type == "response.incomplete";
}

// Detection stays centralized rather than delegated per adapter: the marker
// checks are ordered from most to least structurally distinctive across
// dialects, and that cross-adapter ordering is part of the detection
// contract.
LLMProtocol AdapterRegistry::detect(const nlohmann::json& json) {
  // Gemini markers, validated by value shape: a foreign document with e.g. a
  // `candidates` *string* must not lock the stream. Real candidates lists are
  // non-empty arrays of objects.
  if (const auto it = json.find(Keys::Candidates);
      it != json.end() && it->is_array() && !it->empty() && it->front().is_object()) {
    return LLMProtocol::GeminiGenerateContent;
  }
  if (const auto it = json.find(Keys::UsageMetadata); it != json.end() && it->is_object()) {
    return LLMProtocol::GeminiGenerateContent;
  }
  if (readString(json, Keys::ModelVersion).has_value()) {
    return LLMProtocol::GeminiGenerateContent;
  }

  // OpenAI Chat Completions and non-streaming Responses discriminate on
  // `object`; Responses streaming events discriminate on `type` ("response.*").
  if (const auto object = readString(json, Keys::ObjectKey); object.has_value()) {
    if (absl::StartsWith(object.value(), "chat.completion")) {
      return LLMProtocol::OpenAiChatCompletions;
    }
    if (object.value() == "response") {
      return LLMProtocol::OpenAiResponses;
    }
  }

  if (const auto type = readString(json, Keys::Type); type.has_value()) {
    const absl::string_view type_view = type.value();
    if (absl::StartsWith(type_view, "response.")) {
      return LLMProtocol::OpenAiResponses;
    }
    // Anthropic markers need their documented companion structure: bare
    // `type` strings are generic, and a genuine stream always presents
    // message_start (nested Message) or a non-streaming Message (role/usage)
    // before any usage, so skipping the bare event types loses nothing.
    bool discard = false;
    if (type_view == "message") {
      if (readString(json, Keys::Role).has_value() ||
          readObject(json, Keys::Usage, discard) != nullptr) {
        return LLMProtocol::AnthropicMessages;
      }
    } else if (type_view == "message_start") {
      if (readObject(json, Keys::Message, discard) != nullptr) {
        return LLMProtocol::AnthropicMessages;
      }
    } else if (type_view == "message_delta") {
      if (readObject(json, Keys::Usage, discard) != nullptr ||
          readObject(json, Keys::Delta, discard) != nullptr) {
        return LLMProtocol::AnthropicMessages;
      }
    }
    // `message_stop`/`content_block_*` carry no structure and no usage.
  }

  return LLMProtocol::Unspecified;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
