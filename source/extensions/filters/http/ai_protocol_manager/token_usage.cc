#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/http/ai_protocol_manager/api_protocol_adapter.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// One of the three per-value ApiProtocol maps; the proto <-> internal pair
// lives in filter.h (protocolFromProto/protocolToProto). Kept as separate
// exhaustive switches so a new enum value fails the build at each.
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

void TokenUsage::finalize(const ApiProtocolAdapter& adapter) {
  // Canonicalization rules are per dialect: the adapter must be the
  // accumulator's own (finalizeUsage() in api_protocol_adapter.h wires this).
  ASSERT(adapter.protocol() == api_protocol);
  // The summations below must not run twice.
  ASSERT(!finalized_);
  if (finalized_) {
    return;
  }
  finalized_ = true;
  // Canonicalize the accumulated native counts per dialect (see the header
  // for why this must not run per event).
  adapter.canonicalizeUsage(*this, canonicalization_overflow_);

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

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
