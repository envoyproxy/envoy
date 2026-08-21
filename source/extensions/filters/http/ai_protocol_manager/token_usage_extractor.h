#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Internal mirror of :ref:`envoy.type.ai.v3.ApiProtocol`: the AI API
// contract a response speaks. The two OpenAI protocols share extraction
// logic but differ in streaming event semantics and detection markers.
enum class ApiProtocol {
  Unspecified,
  OpenAiChatCompletions,
  OpenAiResponses,
  AnthropicMessages,
  GeminiGenerateContent,
};

// The envoy.type.ai.v3.ApiProtocol enum-value name for a protocol
// (e.g. "OPENAI_CHAT_COMPLETIONS"), used by the Struct metadata projection.
absl::string_view apiProtocolName(ApiProtocol protocol);

// Normalized LLM token usage accumulated over a response. Every field is
// optional: dialects report different subsets, across several events.
//
// Lifecycle: while a response streams, the fields hold the dialect's *native*
// counts, accumulated with last-wins merge() per field. Native fields are
// independently optional and cumulative, so canonicalization must not run per
// event -- a later partial update repeating one native field would regress an
// already-canonicalized value. finalize(), called once after the last update,
// rewrites the fields onto the canonical *inclusive* contract:
//   input_tokens  = ALL input (uncached + cached reads + cache writes +
//                   tool-use prompt tokens)
//   output_tokens = ALL output, reasoning/thought tokens included
//   total_tokens  = input_tokens + output_tokens, present only when both
//                   components are known; the provider's own total is always
//                   preserved separately as provider_total_tokens.
// Breakdown fields (cached/cache_creation/tool_use input, reasoning output)
// are subsets of the canonical values. The contract normalizes inclusion
// semantics only -- not tokenizer, model, or pricing differences.
struct TokenUsage {
  std::optional<uint64_t> input_tokens;
  std::optional<uint64_t> output_tokens;
  std::optional<uint64_t> total_tokens;
  std::optional<uint64_t> cached_input_tokens;
  std::optional<uint64_t> cache_creation_input_tokens;
  std::optional<uint64_t> tool_use_input_tokens;
  std::optional<uint64_t> reasoning_tokens;
  // The total as reported by the provider, moved here by finalize() (during
  // accumulation the native total rides total_tokens); always preserved,
  // whether or not it agrees with the canonical sum.
  std::optional<uint64_t> provider_total_tokens;
  std::string model;
  ApiProtocol api_protocol{ApiProtocol::Unspecified};

  bool hasAny() const {
    return input_tokens.has_value() || output_tokens.has_value() || total_tokens.has_value() ||
           cached_input_tokens.has_value() || cache_creation_input_tokens.has_value() ||
           tool_use_input_tokens.has_value() || reasoning_tokens.has_value() ||
           provider_total_tokens.has_value();
  }

  // Field-wise merge, later non-empty value wins: one rule covers OpenAI's
  // single terminal usage, Anthropic's split input/cumulative output, and
  // Gemini's repeated cumulative snapshots.
  void merge(const TokenUsage& update);

  // Canonicalize the accumulated native counts (see the struct comment).
  // Must be called exactly once, after the last merge(); repeated calls are
  // no-ops (the summation must not run twice) and assert in debug builds.
  void finalize();

  // True when finalize() dropped a component or total whose sum exceeded the
  // exactly-representable double bound; the caller must publish the record as
  // partial. Meaningful only after finalize().
  bool canonicalizationOverflow() const { return canonicalization_overflow_; }

private:
  bool finalized_{false};
  bool canonicalization_overflow_{false};
};

// One document's extraction outcome. A *known* usage field that is present
// but unusable reads as absent (fail-open) and sets `malformed`, so a corrupt
// final cumulative document cannot leave an earlier snapshot published as
// complete.
struct ExtractionResult {
  TokenUsage usage;
  bool malformed{false};
};

// Stateless extraction of token usage from a parsed response document (the
// nlohmann DOM produced by JsonWithExtBufParser) -- one SSE event's data
// payload, one streamed-array element, or a whole JSON body. Oversized string
// values may appear as external-reference binary nodes; the extractor never
// reads large strings, so they are simply skipped.
class TokenUsageExtractor {
public:
  // Detect the API dialect from a response document's shape. Detection is
  // stream-global once locked, so only strongly shaped, value-validated
  // markers decide; anything else stays Unknown for a later document.
  static ApiProtocol detectFormat(const nlohmann::json& json);

  // Extract the partial *native* usage this document carries (see the
  // TokenUsage lifecycle comment). Absent fields stay unset; the caller
  // accumulates via TokenUsage::merge() with canonicalization in finalize().
  static ExtractionResult extract(ApiProtocol format, const nlohmann::json& json);

  // True when this document marks the dialect's logical end of extraction
  // (Anthropic `message_stop`, OpenAI Responses terminal lifecycle events).
  // Callers must extract() first: terminal Responses events also carry the
  // usage. Chat Completions' non-JSON `[DONE]` is handled before parsing.
  static bool isTerminalEvent(ApiProtocol format, const nlohmann::json& json);
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
