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

// Caps peer-controlled strings so one record cannot become a multi-megabyte metadata value.
constexpr size_t MaxStringValueSize = 256;

// Whether a present-but-null object position is benignly absent or malformed.
// AllowNullAsAbsent is reserved for positions whose wire format documents
// null as a placeholder (OpenAI's `"usage": null`, nullable details objects);
// required structural members (`message`, a terminal event's `response`) are
// malformed when null.
enum class NullPolicy { AllowNullAsAbsent, NullIsMalformed };

// Reads a count (integer or integral double, at most MaxSafeCount). A present but unusable
// value, null included unless `null_policy` allows it, reads as absent and sets `malformed`,
// so a corrupt final update is never published as complete.
std::optional<uint64_t> readCount(const nlohmann::json& json, absl::string_view key,
                                  bool& malformed,
                                  NullPolicy null_policy = NullPolicy::NullIsMalformed);

// Reads a non-empty string of at most MaxStringValueSize. The second overload also flags a
// present non-string (an offloaded reference included) or oversized value; null and "" do not.
// The first stays silent for shape probes, where a mismatch means another dialect.
std::optional<std::string> readString(const nlohmann::json& json, absl::string_view key);
std::optional<std::string> readString(const nlohmann::json& json, absl::string_view key,
                                      bool& malformed);

// A present value of the wrong type sets `malformed`; null reads as absent.
std::optional<bool> readBool(const nlohmann::json& json, absl::string_view key, bool& malformed);
std::optional<uint32_t> readArrayLength(const nlohmann::json& json, absl::string_view key,
                                        bool& malformed);

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
constexpr absl::string_view Stream = "stream";
constexpr absl::string_view MaxTokens = "max_tokens";
constexpr absl::string_view MaxCompletionTokens = "max_completion_tokens";
constexpr absl::string_view MaxOutputTokens = "max_output_tokens";
constexpr absl::string_view MaxOutputTokensCamel = "maxOutputTokens";
constexpr absl::string_view GenerationConfig = "generationConfig";
constexpr absl::string_view GenerationConfigSnake = "generation_config";
constexpr absl::string_view Messages = "messages";
constexpr absl::string_view Input = "input";
constexpr absl::string_view Contents = "contents";
constexpr absl::string_view Tools = "tools";
} // namespace Keys

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
