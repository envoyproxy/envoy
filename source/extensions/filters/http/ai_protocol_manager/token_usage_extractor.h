#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "envoy/json/json_object.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// The AI API dialect a response speaks.
enum class ApiFormat { Unknown, OpenAi, Anthropic, Gemini };

absl::string_view apiFormatName(ApiFormat format);

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
//   total_tokens  = input_tokens + output_tokens; the provider-reported total
//                   is the fallback when a component is missing, and surfaces
//                   as reported_total_tokens when it disagrees.
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
  // Set by finalize() when the provider-reported total disagrees with the
  // canonical input + output sum (which wins total_tokens).
  std::optional<uint64_t> reported_total_tokens;
  std::string model;
  ApiFormat api_format{ApiFormat::Unknown};

  bool hasAny() const {
    return input_tokens.has_value() || output_tokens.has_value() || total_tokens.has_value() ||
           cached_input_tokens.has_value() || cache_creation_input_tokens.has_value() ||
           tool_use_input_tokens.has_value() || reasoning_tokens.has_value();
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

// Stateless extraction of token usage from a parsed response document -- one SSE
// event's data payload, one streamed-array element, or a whole JSON body.
class TokenUsageExtractor {
public:
  // Detect the API dialect from a response document's shape. Detection is
  // stream-global once locked, so only strongly shaped, value-validated
  // markers decide; anything else stays Unknown for a later document.
  static ApiFormat detectFormat(const Json::Object& json);

  // Extract the partial *native* usage this document carries (see the
  // TokenUsage lifecycle comment). Absent fields stay unset; the caller
  // accumulates via TokenUsage::merge() with canonicalization in finalize().
  static ExtractionResult extract(ApiFormat format, const Json::Object& json);

  // True when this document marks the dialect's logical end of extraction
  // (Anthropic `message_stop`, OpenAI Responses terminal lifecycle events).
  // Callers must extract() first: terminal Responses events also carry the
  // usage. Chat Completions' non-JSON `[DONE]` is handled before parsing.
  static bool isTerminalEvent(ApiFormat format, const Json::Object& json);
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
