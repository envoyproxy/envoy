#pragma once

#include "envoy/type/ai/v3/api_protocol.pb.h"

#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Two exhaustive switches rather than one table so a new enum value fails the build.

// Unrecognized values (version skew; configs are validated defined_only) auto-detect.
inline ApiProtocol protocolFromProto(envoy::type::ai::v3::ApiProtocol protocol) {
  switch (protocol) {
  case envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS:
    return ApiProtocol::OpenAiChatCompletions;
  case envoy::type::ai::v3::OPENAI_RESPONSES:
    return ApiProtocol::OpenAiResponses;
  case envoy::type::ai::v3::ANTHROPIC_MESSAGES:
    return ApiProtocol::AnthropicMessages;
  case envoy::type::ai::v3::GEMINI_GENERATE_CONTENT:
    return ApiProtocol::GeminiGenerateContent;
  default:
    return ApiProtocol::Unspecified;
  }
}

inline envoy::type::ai::v3::ApiProtocol protocolToProto(ApiProtocol protocol) {
  switch (protocol) {
  case ApiProtocol::OpenAiChatCompletions:
    return envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS;
  case ApiProtocol::OpenAiResponses:
    return envoy::type::ai::v3::OPENAI_RESPONSES;
  case ApiProtocol::AnthropicMessages:
    return envoy::type::ai::v3::ANTHROPIC_MESSAGES;
  case ApiProtocol::GeminiGenerateContent:
    return envoy::type::ai::v3::GEMINI_GENERATE_CONTENT;
  case ApiProtocol::Unspecified:
    break;
  }
  return envoy::type::ai::v3::API_PROTOCOL_UNSPECIFIED;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
