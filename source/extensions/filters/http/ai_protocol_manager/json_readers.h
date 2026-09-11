#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
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
std::optional<uint64_t> readCount(const nlohmann::json& json, absl::string_view key,
                                  bool& malformed);

// Read a non-empty string value of at most MaxStringValueSize; anything else
// reads as absent.
std::optional<std::string> readString(const nlohmann::json& json, absl::string_view key);

// Read a nested object, applying the null policy above.
const nlohmann::json* readObject(const nlohmann::json& json, absl::string_view key, bool& malformed,
                                 NullPolicy null_policy = NullPolicy::NullIsMalformed);

// Adds an optional adjunct onto a base count. An absent adjunct leaves the
// base as it is; an absent base with a present adjunct reads as zero, because
// a dialect omits its native bucket when the model produced none of it and
// the adjunct still belongs in the canonical count. A sum above the
// metadata-safe bound is dropped rather than published imprecisely, and
// reported through `overflow` so the caller flags the record instead of
// silently omitting a canonical component.
std::optional<uint64_t> addCounts(std::optional<uint64_t> base,
                                  const std::optional<uint64_t>& extra, bool& overflow);

// nlohmann's object map has a transparent comparator, so string_view lookups don't allocate.
namespace Keys {
constexpr absl::string_view PromptTokens = "prompt_tokens";
constexpr absl::string_view CompletionTokens = "completion_tokens";
constexpr absl::string_view TotalTokens = "total_tokens";
constexpr absl::string_view InputTokens = "input_tokens";
constexpr absl::string_view OutputTokens = "output_tokens";
constexpr absl::string_view PromptTokensDetails = "prompt_tokens_details";
constexpr absl::string_view InputTokensDetails = "input_tokens_details";
constexpr absl::string_view CompletionTokensDetails = "completion_tokens_details";
constexpr absl::string_view OutputTokensDetails = "output_tokens_details";
constexpr absl::string_view CachedTokens = "cached_tokens";
constexpr absl::string_view CacheWriteTokens = "cache_write_tokens";
constexpr absl::string_view ReasoningTokens = "reasoning_tokens";
constexpr absl::string_view CacheReadInputTokens = "cache_read_input_tokens";
constexpr absl::string_view CacheCreationInputTokens = "cache_creation_input_tokens";
constexpr absl::string_view ThinkingTokens = "thinking_tokens";
constexpr absl::string_view UsageMetadata = "usageMetadata";
constexpr absl::string_view PromptTokenCount = "promptTokenCount";
constexpr absl::string_view CandidatesTokenCount = "candidatesTokenCount";
constexpr absl::string_view TotalTokenCount = "totalTokenCount";
constexpr absl::string_view CachedContentTokenCount = "cachedContentTokenCount";
constexpr absl::string_view ThoughtsTokenCount = "thoughtsTokenCount";
constexpr absl::string_view ToolUsePromptTokenCount = "toolUsePromptTokenCount";
constexpr absl::string_view ModelVersion = "modelVersion";
constexpr absl::string_view Candidates = "candidates";
constexpr absl::string_view Usage = "usage";
constexpr absl::string_view Message = "message";
constexpr absl::string_view Model = "model";
constexpr absl::string_view Response = "response";
constexpr absl::string_view ObjectKey = "object";
constexpr absl::string_view Type = "type";
constexpr absl::string_view Role = "role";
constexpr absl::string_view Delta = "delta";
} // namespace Keys

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
