#include "source/extensions/filters/http/ai_protocol_manager/token_usage_extractor.h"

#include <cmath>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/singleton/const_singleton.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// Keys materialized once: nlohmann's object map is keyed by std::string
// without a transparent comparator, so per-probe temporaries would allocate on
// the hot path.
struct JsonKeyValues {
  const std::string PromptTokens{"prompt_tokens"};
  const std::string CompletionTokens{"completion_tokens"};
  const std::string TotalTokens{"total_tokens"};
  const std::string InputTokens{"input_tokens"};
  const std::string OutputTokens{"output_tokens"};
  const std::string PromptTokensDetails{"prompt_tokens_details"};
  const std::string InputTokensDetails{"input_tokens_details"};
  const std::string CompletionTokensDetails{"completion_tokens_details"};
  const std::string OutputTokensDetails{"output_tokens_details"};
  const std::string CachedTokens{"cached_tokens"};
  const std::string CacheWriteTokens{"cache_write_tokens"};
  const std::string ReasoningTokens{"reasoning_tokens"};
  const std::string CacheReadInputTokens{"cache_read_input_tokens"};
  const std::string CacheCreationInputTokens{"cache_creation_input_tokens"};
  const std::string ThinkingTokens{"thinking_tokens"};
  const std::string UsageMetadata{"usageMetadata"};
  const std::string PromptTokenCount{"promptTokenCount"};
  const std::string CandidatesTokenCount{"candidatesTokenCount"};
  const std::string TotalTokenCount{"totalTokenCount"};
  const std::string CachedContentTokenCount{"cachedContentTokenCount"};
  const std::string ThoughtsTokenCount{"thoughtsTokenCount"};
  const std::string ToolUsePromptTokenCount{"toolUsePromptTokenCount"};
  const std::string ModelVersion{"modelVersion"};
  const std::string Candidates{"candidates"};
  const std::string Usage{"usage"};
  const std::string Message{"message"};
  const std::string Model{"model"};
  const std::string Response{"response"};
  const std::string ObjectKey{"object"};
  const std::string Type{"type"};
  const std::string Role{"role"};
  const std::string Delta{"delta"};
};
using JsonKeys = ConstSingleton<JsonKeyValues>;

// Counts are published into a protobuf `number_value` (an IEEE double) in the
// Struct projection; only values a double represents exactly survive that
// round trip. Anything above this bound, or non-finite, negative, or
// fractional, is rejected rather than coerced. The bound also keeps summed
// counts far from uint64_t overflow.
constexpr uint64_t MaxSafeCount = (uint64_t(1) << 53) - 1;

// Read a token count (integer or JSON double). Returns nullopt for a missing
// key; a key that is present but unusable -- wrong type, container, null
// (no dialect documents null counts), negative, fractional, or out of range
// -- also sets `malformed`, so a corrupt final cumulative update cannot leave
// an earlier value published as complete.
std::optional<uint64_t> readCount(const nlohmann::json& json, const std::string& key,
                                  bool& malformed) {
  const auto it = json.find(key);
  if (it == json.end()) {
    return std::nullopt;
  }
  const nlohmann::json& value = *it;
  // JsonWithExtBufParser stores any literal that fits int64 as a *signed*
  // integer (is_number_unsigned() is true only above INT64_MAX), so probe the
  // signed representation first.
  if (value.is_number_integer()) {
    if (value.is_number_unsigned()) {
      const uint64_t count = value.get<uint64_t>();
      if (count > MaxSafeCount) {
        malformed = true;
        return std::nullopt;
      }
      return count;
    }
    const int64_t count = value.get<int64_t>();
    if (count < 0 || static_cast<uint64_t>(count) > MaxSafeCount) {
      malformed = true;
      return std::nullopt;
    }
    return static_cast<uint64_t>(count);
  }
  if (value.is_number_float()) {
    const double as_double = value.get<double>();
    // Range-check before the float-to-integer cast: converting an
    // out-of-range double to uint64_t is undefined behavior.
    if (!std::isfinite(as_double) || as_double < 0 ||
        as_double > static_cast<double>(MaxSafeCount) || std::trunc(as_double) != as_double) {
      malformed = true;
      return std::nullopt;
    }
    return static_cast<uint64_t>(as_double);
  }
  malformed = true; // Present with a non-numeric value (string, bool, null, container).
  return std::nullopt;
}

// Response strings are upstream-controlled; the cap keeps one response from
// turning into a multi-megabyte metadata value or access-log entry. A string
// offloaded as an external reference is not a string node and reads as
// absent.
constexpr size_t MaxStringValueSize = 256;

std::optional<std::string> readString(const nlohmann::json& json, const std::string& key) {
  const auto it = json.find(key);
  if (it == json.end() || !it->is_string()) {
    return std::nullopt;
  }
  const auto& value = it->get_ref<const std::string&>();
  if (value.empty() || value.size() > MaxStringValueSize) {
    return std::nullopt;
  }
  return value;
}

// Whether a present-but-null object position is benignly absent or malformed.
// AllowNullAsAbsent is reserved for positions whose wire format documents
// null as a placeholder (OpenAI's `"usage": null`, nullable details objects);
// required structural members (`message`, a terminal event's `response`) are
// malformed when null.
enum class NullPolicy { AllowNullAsAbsent, NullIsMalformed };

const nlohmann::json* readObject(const nlohmann::json& json, const std::string& key,
                                 bool& malformed,
                                 NullPolicy null_policy = NullPolicy::NullIsMalformed) {
  const auto it = json.find(key);
  if (it == json.end()) {
    return nullptr;
  }
  if (it->is_object()) {
    return &*it;
  }
  if (!it->is_null() || null_policy == NullPolicy::NullIsMalformed) {
    malformed = true;
  }
  return nullptr;
}

// Adds an optional adjunct onto a base count. A sum above the metadata-safe
// bound is dropped rather than published imprecisely, and reported through
// `overflow` so the caller flags the record instead of silently omitting a
// canonical component.
std::optional<uint64_t> addCounts(std::optional<uint64_t> base,
                                  const std::optional<uint64_t>& extra, bool& overflow) {
  if (!base.has_value() || !extra.has_value()) {
    return base;
  }
  const uint64_t sum = base.value() + extra.value();
  if (sum > MaxSafeCount) {
    overflow = true;
    return std::nullopt;
  }
  return sum;
}

// OpenAI: Chat Completions and Responses API. The two dialects share one
// structure with renamed keys; they never mix in one document, so reading
// either name from the same usage object is unambiguous. Responses API
// streaming lifecycle events nest the payload under `response`.
TokenUsage extractOpenAi(ApiProtocol protocol, const nlohmann::json& json, bool& malformed) {
  TokenUsage usage;
  usage.api_protocol = protocol;

  const nlohmann::json* response = readObject(json, JsonKeys::get().Response, malformed);
  const nlohmann::json& node = response != nullptr ? *response : json;

  if (auto model = readString(node, JsonKeys::get().Model); model.has_value()) {
    usage.model = std::move(model).value();
  }

  const nlohmann::json* usage_node =
      readObject(node, JsonKeys::get().Usage, malformed, NullPolicy::AllowNullAsAbsent);
  if (usage_node == nullptr) {
    return usage;
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

  const nlohmann::json* input_details = readObject(*usage_node, JsonKeys::get().PromptTokensDetails,
                                                   malformed, NullPolicy::AllowNullAsAbsent);
  if (input_details == nullptr) {
    input_details = readObject(*usage_node, JsonKeys::get().InputTokensDetails, malformed,
                               NullPolicy::AllowNullAsAbsent);
  }
  if (input_details != nullptr) {
    usage.cached_input_tokens = readCount(*input_details, JsonKeys::get().CachedTokens, malformed);
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
    usage.reasoning_tokens = readCount(*output_details, JsonKeys::get().ReasoningTokens, malformed);
  }
  return usage;
}

// Anthropic Messages API. Non-streaming responses and `message_delta` events
// carry `usage` at the root; `message_start` nests a Message object (with
// `model` and the input-side usage) under `message`. `message_delta` counts
// are cumulative, which the caller's last-wins merge handles.
TokenUsage extractAnthropic(const nlohmann::json& json, bool& malformed) {
  TokenUsage usage;
  usage.api_protocol = ApiProtocol::AnthropicMessages;

  const nlohmann::json* message = readObject(json, JsonKeys::get().Message, malformed);
  const nlohmann::json& node = message != nullptr ? *message : json;

  if (auto model = readString(node, JsonKeys::get().Model); model.has_value()) {
    usage.model = std::move(model).value();
  }

  const nlohmann::json* usage_node = readObject(node, JsonKeys::get().Usage, malformed);
  if (usage_node == nullptr) {
    return usage;
  }

  // Native counts only: the disjoint input/cache buckets are summed once, in
  // finalize() -- summing per event would let a partial update regress the
  // accumulated value via last-wins merge.
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
  return usage;
}

// Gemini generateContent / streamGenerateContent. Every chunk is a
// GenerateContentResponse; `usageMetadata` snapshots are cumulative (last
// wins). `cachedContentTokenCount` is a subset of `promptTokenCount`, so it
// maps to cached_input_tokens without any arithmetic.
TokenUsage extractGemini(const nlohmann::json& json, bool& malformed) {
  TokenUsage usage;
  usage.api_protocol = ApiProtocol::GeminiGenerateContent;

  if (auto model = readString(json, JsonKeys::get().ModelVersion); model.has_value()) {
    usage.model = std::move(model).value();
  }

  const nlohmann::json* usage_node = readObject(json, JsonKeys::get().UsageMetadata, malformed);
  if (usage_node == nullptr) {
    return usage;
  }

  // Native counts only; the tool-use and thoughts adjuncts are summed in at
  // finalize(), after the last cumulative snapshot merged.
  usage.input_tokens = readCount(*usage_node, JsonKeys::get().PromptTokenCount, malformed);
  usage.output_tokens = readCount(*usage_node, JsonKeys::get().CandidatesTokenCount, malformed);
  usage.total_tokens = readCount(*usage_node, JsonKeys::get().TotalTokenCount, malformed);
  usage.cached_input_tokens =
      readCount(*usage_node, JsonKeys::get().CachedContentTokenCount, malformed);
  usage.tool_use_input_tokens =
      readCount(*usage_node, JsonKeys::get().ToolUsePromptTokenCount, malformed);
  usage.reasoning_tokens = readCount(*usage_node, JsonKeys::get().ThoughtsTokenCount, malformed);
  return usage;
}

} // namespace

absl::string_view apiProtocolName(ApiProtocol protocol) {
  switch (protocol) {
  case ApiProtocol::OpenAiChatCompletions:
    return "OPENAI_CHAT_COMPLETIONS";
  case ApiProtocol::OpenAiResponses:
    return "OPENAI_RESPONSES";
  case ApiProtocol::AnthropicMessages:
    return "ANTHROPIC_MESSAGES";
  case ApiProtocol::GeminiGenerateContent:
    return "GEMINI_GENERATE_CONTENT";
  case ApiProtocol::Unspecified:
    break;
  }
  return "API_PROTOCOL_UNSPECIFIED";
}

void TokenUsage::merge(const TokenUsage& update) {
  // Native updates must not land on a canonicalized accumulator.
  ASSERT(!finalized_);
  const auto take = [](std::optional<uint64_t>& current, const std::optional<uint64_t>& next) {
    if (next.has_value()) {
      current = next;
    }
  };
  take(input_tokens, update.input_tokens);
  take(output_tokens, update.output_tokens);
  take(total_tokens, update.total_tokens);
  take(cached_input_tokens, update.cached_input_tokens);
  take(cache_creation_input_tokens, update.cache_creation_input_tokens);
  take(tool_use_input_tokens, update.tool_use_input_tokens);
  take(reasoning_tokens, update.reasoning_tokens);
  if (!update.model.empty()) {
    model = update.model;
  }
  if (update.api_protocol != ApiProtocol::Unspecified) {
    api_protocol = update.api_protocol;
  }
}

void TokenUsage::finalize() {
  // The summations below must not run twice.
  ASSERT(!finalized_);
  if (finalized_) {
    return;
  }
  finalized_ = true;
  // Canonicalize the accumulated native counts per dialect (see the header
  // for why this must not run per event).
  switch (api_protocol) {
  case ApiProtocol::AnthropicMessages:
    // Native input excludes the two disjoint cache buckets.
    if (input_tokens.has_value()) {
      input_tokens =
          addCounts(addCounts(input_tokens, cached_input_tokens, canonicalization_overflow_),
                    cache_creation_input_tokens, canonicalization_overflow_);
    }
    break;
  case ApiProtocol::GeminiGenerateContent:
    // Native prompt/candidates counts exclude tool-use and thoughts.
    if (input_tokens.has_value()) {
      input_tokens = addCounts(input_tokens, tool_use_input_tokens, canonicalization_overflow_);
    }
    if (output_tokens.has_value()) {
      output_tokens = addCounts(output_tokens, reasoning_tokens, canonicalization_overflow_);
    }
    break;
  case ApiProtocol::OpenAiChatCompletions:
  case ApiProtocol::OpenAiResponses:
  case ApiProtocol::Unspecified:
    // OpenAI's native counts are already inclusive.
    break;
  }

  // The provider-reported total (riding total_tokens during accumulation)
  // moves to its own field, always preserved. total_tokens is exclusively the
  // canonical sum, present only when both components are known and the sum
  // round-trips exactly through the double-backed metadata field -- so its
  // meaning never depends on what else was reported.
  provider_total_tokens = total_tokens;
  total_tokens.reset();
  if (input_tokens.has_value() && output_tokens.has_value()) {
    const uint64_t sum = input_tokens.value() + output_tokens.value();
    if (sum <= MaxSafeCount) {
      total_tokens = sum;
    } else {
      // Not exactly representable in the double-backed Struct projection:
      // publish no canonical total and flag the record.
      canonicalization_overflow_ = true;
    }
  }
}

ApiProtocol TokenUsageExtractor::detectFormat(const nlohmann::json& json) {
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

ExtractionResult TokenUsageExtractor::extract(ApiProtocol format, const nlohmann::json& json) {
  ExtractionResult result;
  switch (format) {
  case ApiProtocol::OpenAiChatCompletions:
  case ApiProtocol::OpenAiResponses:
    result.usage = extractOpenAi(format, json, result.malformed);
    break;
  case ApiProtocol::AnthropicMessages:
    result.usage = extractAnthropic(json, result.malformed);
    break;
  case ApiProtocol::GeminiGenerateContent:
    result.usage = extractGemini(json, result.malformed);
    break;
  case ApiProtocol::Unspecified:
    break;
  }
  return result;
}

bool TokenUsageExtractor::isTerminalEvent(ApiProtocol format, const nlohmann::json& json) {
  const auto type = readString(json, JsonKeys::get().Type);
  if (!type.has_value()) {
    return false;
  }
  switch (format) {
  case ApiProtocol::AnthropicMessages:
    return type.value() == "message_stop";
  case ApiProtocol::OpenAiResponses:
    // Terminal lifecycle events; also the usage carriers, so callers
    // extract() first.
    return type.value() == "response.completed" || type.value() == "response.failed" ||
           type.value() == "response.incomplete";
  case ApiProtocol::OpenAiChatCompletions:
    // Terminates with the non-JSON `[DONE]` sentinel, handled before parsing.
  case ApiProtocol::GeminiGenerateContent:
    // No in-band terminator; extraction finalizes at end of stream.
  case ApiProtocol::Unspecified:
    break;
  }
  return false;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
