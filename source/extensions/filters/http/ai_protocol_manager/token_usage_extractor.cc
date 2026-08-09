#include "source/extensions/filters/http/ai_protocol_manager/token_usage_extractor.h"

#include <cmath>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/singleton/const_singleton.h"

#include "absl/strings/match.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// Keys materialized once: the Json::Object accessors take `const
// std::string&`, and per-probe temporaries would allocate on the hot path.
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

// Counts are published into a protobuf `number_value` (an IEEE double); only
// values a double represents exactly survive the round trip. Anything above
// this bound, or non-finite, negative, or fractional, is rejected rather than
// coerced. The bound also keeps summed counts far from uint64_t overflow.
constexpr uint64_t MaxSafeCount = (uint64_t(1) << 53) - 1;

// Distinguishes a present container-typed member (object/array) from a
// present-but-null member on a lookup-failure path; used by readObject() to
// apply its per-position null policy.
bool presentValueIsContainer(const Json::Object& json, const std::string& key) {
  bool container = false;
  json.iterate([&container, &key](const std::string& member, const Json::Object& value) {
        if (member == key) {
          container = value.isObject() || value.isArray();
          return false; // Stop iterating.
        }
        return true;
      })
      .IgnoreError(); // The receiver is object-checked by every caller.
  return container;
}

// Read a token count (integer or JSON double). Returns nullopt for a missing
// key; a key that is present but unusable also sets `malformed`.
std::optional<uint64_t> readCount(const Json::Object& json, const std::string& key,
                                  bool& malformed) {
  // Missing fields are the normal case: a presence gate keeps a miss to a map
  // lookup instead of a formatted error-status allocation.
  if (!json.hasObject(key)) {
    return std::nullopt;
  }
  auto value_or = json.getValue(key);
  if (!value_or.ok()) {
    // Present but object, array, or null -- all malformed for a count: no
    // dialect documents null counts, so a null here is indistinguishable from
    // a corrupt final update.
    malformed = true;
    return std::nullopt;
  }
  const Json::ValueType& value = value_or.value();
  if (const int64_t* as_int = absl::get_if<int64_t>(&value); as_int != nullptr) {
    if (*as_int < 0 || static_cast<uint64_t>(*as_int) > MaxSafeCount) {
      malformed = true;
      return std::nullopt;
    }
    return static_cast<uint64_t>(*as_int);
  }
  if (const double* as_double = absl::get_if<double>(&value); as_double != nullptr) {
    // Range-check before the float-to-integer cast: converting an
    // out-of-range double to uint64_t is undefined behavior.
    if (!std::isfinite(*as_double) || *as_double < 0 ||
        *as_double > static_cast<double>(MaxSafeCount) || std::trunc(*as_double) != *as_double) {
      malformed = true;
      return std::nullopt;
    }
    return static_cast<uint64_t>(*as_double);
  }
  malformed = true; // Present with a non-numeric scalar type (string, bool).
  return std::nullopt;
}

// Response strings are upstream-controlled; the cap keeps one response from
// turning into a multi-megabyte metadata value or access-log entry.
constexpr size_t MaxStringValueSize = 256;

std::optional<std::string> readString(const Json::Object& json, const std::string& key) {
  if (!json.hasObject(key)) {
    return std::nullopt;
  }
  auto value_or = json.getString(key);
  if (!value_or.ok() || value_or.value().empty() || value_or.value().size() > MaxStringValueSize) {
    return std::nullopt;
  }
  return value_or.value();
}

// Whether a present-but-null object position is benignly absent or malformed.
// AllowNullAsAbsent is reserved for positions whose wire format documents
// null as a placeholder (OpenAI's `"usage": null`, nullable details objects);
// required structural members (`message`, a terminal event's `response`) are
// malformed when null.
enum class NullPolicy { AllowNullAsAbsent, NullIsMalformed };

Json::ObjectSharedPtr readObject(const Json::Object& json, const std::string& key, bool& malformed,
                                 NullPolicy null_policy = NullPolicy::NullIsMalformed) {
  if (!json.hasObject(key)) {
    return nullptr;
  }
  auto object_or = json.getObject(key);
  if (!object_or.ok()) {
    // Present but not an object: scalars and arrays are always malformed;
    // null follows the position's documented nullability.
    if (json.getValue(key).ok() || presentValueIsContainer(json, key) ||
        null_policy == NullPolicy::NullIsMalformed) {
      malformed = true;
    }
    return nullptr;
  }
  return object_or.value();
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
// structure with renamed keys (prompt_tokens/completion_tokens vs
// input_tokens/output_tokens); they never mix in one document, so reading
// either name from the same usage object is unambiguous. Responses API
// streaming lifecycle events nest the payload under `response`.
TokenUsage extractOpenAi(const Json::Object& json, bool& malformed) {
  TokenUsage usage;
  usage.api_format = ApiFormat::OpenAi;

  const Json::ObjectSharedPtr response = readObject(json, JsonKeys::get().Response, malformed);
  const Json::Object& node = response != nullptr ? *response : json;

  if (auto model = readString(node, JsonKeys::get().Model); model.has_value()) {
    usage.model = std::move(model).value();
  }

  const Json::ObjectSharedPtr usage_node =
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

  Json::ObjectSharedPtr input_details = readObject(*usage_node, JsonKeys::get().PromptTokensDetails,
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

  Json::ObjectSharedPtr output_details =
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
TokenUsage extractAnthropic(const Json::Object& json, bool& malformed) {
  TokenUsage usage;
  usage.api_format = ApiFormat::Anthropic;

  const Json::ObjectSharedPtr message = readObject(json, JsonKeys::get().Message, malformed);
  const Json::Object& node = message != nullptr ? *message : json;

  if (auto model = readString(node, JsonKeys::get().Model); model.has_value()) {
    usage.model = std::move(model).value();
  }

  const Json::ObjectSharedPtr usage_node = readObject(node, JsonKeys::get().Usage, malformed);
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

  if (const Json::ObjectSharedPtr details =
          readObject(*usage_node, JsonKeys::get().OutputTokensDetails, malformed,
                     NullPolicy::AllowNullAsAbsent);
      details != nullptr) {
    usage.reasoning_tokens = readCount(*details, JsonKeys::get().ThinkingTokens, malformed);
  }
  return usage;
}

// Gemini generateContent / streamGenerateContent. Every chunk is a
// GenerateContentResponse; `usageMetadata` snapshots are cumulative (last
// wins). `cachedContentTokenCount` is a subset of `promptTokenCount`, so it
// maps to cached_input_tokens without any arithmetic.
TokenUsage extractGemini(const Json::Object& json, bool& malformed) {
  TokenUsage usage;
  usage.api_format = ApiFormat::Gemini;

  if (auto model = readString(json, JsonKeys::get().ModelVersion); model.has_value()) {
    usage.model = std::move(model).value();
  }

  const Json::ObjectSharedPtr usage_node =
      readObject(json, JsonKeys::get().UsageMetadata, malformed);
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

absl::string_view apiFormatName(ApiFormat format) {
  switch (format) {
  case ApiFormat::OpenAi:
    return "openai";
  case ApiFormat::Anthropic:
    return "anthropic";
  case ApiFormat::Gemini:
    return "gemini";
  case ApiFormat::Unknown:
    break;
  }
  return "unknown";
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
  if (update.api_format != ApiFormat::Unknown) {
    api_format = update.api_format;
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
  switch (api_format) {
  case ApiFormat::Anthropic:
    // Native input excludes the two disjoint cache buckets.
    if (input_tokens.has_value()) {
      input_tokens =
          addCounts(addCounts(input_tokens, cached_input_tokens, canonicalization_overflow_),
                    cache_creation_input_tokens, canonicalization_overflow_);
    }
    break;
  case ApiFormat::Gemini:
    // Native prompt/candidates counts exclude tool-use and thoughts.
    if (input_tokens.has_value()) {
      input_tokens = addCounts(input_tokens, tool_use_input_tokens, canonicalization_overflow_);
    }
    if (output_tokens.has_value()) {
      output_tokens = addCounts(output_tokens, reasoning_tokens, canonicalization_overflow_);
    }
    break;
  case ApiFormat::OpenAi:
  case ApiFormat::Unknown:
    // OpenAI's native counts are already inclusive.
    break;
  }

  // Canonical total from the canonical components (total == input + output),
  // published only when it round-trips exactly through the double-backed
  // metadata field. The provider-reported total is the fallback when a
  // component is missing, and surfaces as reported_total_tokens when it
  // disagrees (an inconsistent provider, or a bucket unknown here).
  const std::optional<uint64_t> reported = total_tokens;
  total_tokens.reset();
  const bool components_present = input_tokens.has_value() && output_tokens.has_value();
  if (components_present) {
    const uint64_t sum = input_tokens.value() + output_tokens.value();
    if (sum <= MaxSafeCount) {
      total_tokens = sum;
    } else {
      // Not exactly representable: publish no total (the reported total,
      // which necessarily disagrees, surfaces below) and flag the record.
      canonicalization_overflow_ = true;
    }
  } else {
    total_tokens = reported;
    return;
  }
  if (reported.has_value() &&
      (!total_tokens.has_value() || reported.value() != total_tokens.value())) {
    reported_total_tokens = reported;
  }
}

ApiFormat TokenUsageExtractor::detectFormat(const Json::Object& json) {
  // Gemini markers, validated by value shape: hasObject() checks key presence
  // only, and a foreign document with e.g. a `candidates` *string* must not
  // lock the stream. Real candidates lists are non-empty arrays of objects
  // (getObjectArray() does not type-check elements, so the first is probed).
  bool candidates_shaped = false;
  if (json.hasObject(JsonKeys::get().Candidates)) {
    const auto candidates_or = json.getObjectArray(JsonKeys::get().Candidates);
    candidates_shaped = candidates_or.ok() && !candidates_or.value().empty() &&
                        candidates_or.value()[0] != nullptr && candidates_or.value()[0]->isObject();
  }
  const bool usage_metadata_shaped = json.hasObject(JsonKeys::get().UsageMetadata) &&
                                     json.getObject(JsonKeys::get().UsageMetadata).ok();
  if (usage_metadata_shaped || candidates_shaped ||
      readString(json, JsonKeys::get().ModelVersion).has_value()) {
    return ApiFormat::Gemini;
  }

  // OpenAI Chat Completions and non-streaming Responses discriminate on
  // `object`; Responses streaming events discriminate on `type` ("response.*").
  if (const auto object = readString(json, JsonKeys::get().ObjectKey); object.has_value()) {
    if (absl::StartsWith(object.value(), "chat.completion") || object.value() == "response") {
      return ApiFormat::OpenAi;
    }
  }

  if (const auto type = readString(json, JsonKeys::get().Type); type.has_value()) {
    const absl::string_view type_view = type.value();
    if (absl::StartsWith(type_view, "response.")) {
      return ApiFormat::OpenAi;
    }
    // Anthropic markers need their documented companion structure: bare
    // `type` strings are generic, and a genuine stream always presents
    // message_start (nested Message) or a non-streaming Message (role/usage)
    // before any usage, so skipping structureless types loses nothing.
    bool discard = false;
    if (type_view == "message") {
      if (readString(json, JsonKeys::get().Role).has_value() ||
          readObject(json, JsonKeys::get().Usage, discard) != nullptr) {
        return ApiFormat::Anthropic;
      }
    } else if (type_view == "message_start") {
      if (readObject(json, JsonKeys::get().Message, discard) != nullptr) {
        return ApiFormat::Anthropic;
      }
    } else if (type_view == "message_delta") {
      if (readObject(json, JsonKeys::get().Usage, discard) != nullptr ||
          readObject(json, JsonKeys::get().Delta, discard) != nullptr) {
        return ApiFormat::Anthropic;
      }
    }
  }

  return ApiFormat::Unknown;
}

ExtractionResult TokenUsageExtractor::extract(ApiFormat format, const Json::Object& json) {
  ExtractionResult result;
  switch (format) {
  case ApiFormat::OpenAi:
    result.usage = extractOpenAi(json, result.malformed);
    break;
  case ApiFormat::Anthropic:
    result.usage = extractAnthropic(json, result.malformed);
    break;
  case ApiFormat::Gemini:
    result.usage = extractGemini(json, result.malformed);
    break;
  case ApiFormat::Unknown:
    break;
  }
  return result;
}

bool TokenUsageExtractor::isTerminalEvent(ApiFormat format, const Json::Object& json) {
  const auto type = readString(json, JsonKeys::get().Type);
  if (!type.has_value()) {
    return false;
  }
  switch (format) {
  case ApiFormat::Anthropic:
    return type.value() == "message_stop";
  case ApiFormat::OpenAi:
    // Responses API terminal lifecycle events; also the usage carriers, so
    // callers extract() first. Chat Completions terminates with the non-JSON
    // `[DONE]` sentinel instead, handled before parsing.
    return type.value() == "response.completed" || type.value() == "response.failed" ||
           type.value() == "response.incomplete";
  case ApiFormat::Gemini:
    // No in-band terminator; extraction finalizes at end of stream.
  case ApiFormat::Unknown:
    break;
  }
  return false;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
