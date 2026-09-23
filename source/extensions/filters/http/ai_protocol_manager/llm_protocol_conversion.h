#pragma once

#include "envoy/type/ai/v3/llm_protocol.pb.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Two exhaustive switches rather than one table so a new enum value fails the build.

inline LLMProtocol protocolFromProto(envoy::type::ai::v3::LLMProtocol protocol) {
  switch (protocol) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::type::ai::v3::LLM_PROTOCOL_UNSPECIFIED:
    return LLMProtocol::Unspecified;
  case envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS:
    return LLMProtocol::OpenAiChatCompletions;
  case envoy::type::ai::v3::OPENAI_RESPONSES:
    return LLMProtocol::OpenAiResponses;
  case envoy::type::ai::v3::ANTHROPIC_MESSAGES:
    return LLMProtocol::AnthropicMessages;
  case envoy::type::ai::v3::GEMINI_GENERATE_CONTENT:
    return LLMProtocol::GeminiGenerateContent;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

inline envoy::type::ai::v3::LLMProtocol protocolToProto(LLMProtocol protocol) {
  switch (protocol) {
  case LLMProtocol::OpenAiChatCompletions:
    return envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS;
  case LLMProtocol::OpenAiResponses:
    return envoy::type::ai::v3::OPENAI_RESPONSES;
  case LLMProtocol::AnthropicMessages:
    return envoy::type::ai::v3::ANTHROPIC_MESSAGES;
  case LLMProtocol::GeminiGenerateContent:
    return envoy::type::ai::v3::GEMINI_GENERATE_CONTENT;
  case LLMProtocol::Unspecified:
    break;
  }
  return envoy::type::ai::v3::LLM_PROTOCOL_UNSPECIFIED;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
