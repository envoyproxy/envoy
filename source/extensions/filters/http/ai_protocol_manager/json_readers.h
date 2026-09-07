#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "source/common/singleton/const_singleton.h"

#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Shared, dialect-agnostic readers over a parsed payload document (the
// nlohmann DOM produced by JsonWithExtBufParser). Every protocol adapter is
// built from these primitives; the fail-open semantics (a present-but-unusable
// known field reads as absent and flags `malformed`) are part of the shared
// extraction contract, not any one dialect.

// Sanity bound on provider-reported counts: values must survive an exact
// round trip through IEEE-double representations (JSON re-serialization,
// access-log pipelines) that downstream consumers commonly apply to the
// typed record, and summed counts must stay far from uint64_t overflow.
// Anything above this bound, or non-finite, negative, or fractional, is
// rejected rather than coerced -- real token counts sit many orders of
// magnitude below it.
constexpr uint64_t MaxSafeCount = (uint64_t(1) << 53) - 1;

// Response strings are upstream-controlled; the cap keeps one response from
// turning into a multi-megabyte metadata value or access-log entry. A string
// offloaded as an external reference is not a string node and reads as
// absent.
constexpr size_t MaxStringValueSize = 256;

// Whether a present-but-null object position is benignly absent or malformed.
// AllowNullAsAbsent is reserved for positions whose wire format documents
// null as a placeholder (OpenAI's `"usage": null`, nullable details objects);
// required structural members (`message`, a terminal event's `response`) are
// malformed when null.
enum class NullPolicy { AllowNullAsAbsent, NullIsMalformed };

// Read a token count (integer or JSON double). Returns nullopt for a missing
// key; a key that is present but unusable -- wrong type, container, null
// (no dialect documents null counts), negative, fractional, or out of range
// -- also sets `malformed`, so a corrupt final cumulative update cannot leave
// an earlier value published as complete.
std::optional<uint64_t> readCount(const nlohmann::json& json, const std::string& key,
                                  bool& malformed);

// Read a non-empty string value of at most MaxStringValueSize; anything else
// reads as absent.
std::optional<std::string> readString(const nlohmann::json& json, const std::string& key);

// Read a nested object, applying the null policy above.
const nlohmann::json* readObject(const nlohmann::json& json, const std::string& key,
                                 bool& malformed,
                                 NullPolicy null_policy = NullPolicy::NullIsMalformed);

// Adds an optional adjunct onto a base count. A sum above the metadata-safe
// bound is dropped rather than published imprecisely, and reported through
// `overflow` so the caller flags the record instead of silently omitting a
// canonical component.
std::optional<uint64_t> addCounts(std::optional<uint64_t> base,
                                  const std::optional<uint64_t>& extra, bool& overflow);

// Keys materialized once: nlohmann's object map is keyed by std::string
// without a transparent comparator, so per-probe temporaries would allocate on
// the hot path. The pool is shared across adapters; each adapter reads only
// its dialect's keys.
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

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
