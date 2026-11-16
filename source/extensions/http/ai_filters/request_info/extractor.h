#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

struct RequestAttributes {
  HttpFilters::AiProtocolManager::LLMProtocol llm_protocol{
      HttpFilters::AiProtocolManager::LLMProtocol::Unspecified};
  std::string model;
  std::optional<bool> stream;
  std::optional<uint64_t> max_output_tokens;
  std::optional<uint32_t> message_count;
  std::optional<uint32_t> tool_count;
  // Set when a present attribute was unusable and so reads as absent.
  bool malformed{false};
};

// TODO(botengyao): read one normalized schema once a transcoder produces an IR; the per-API
// readers then go away.
// `arrived` is what the request's path named under the binding it came in, when one matched:
// the model and the streaming mode it decided take precedence over the body's.
RequestAttributes
extractRequestAttributes(HttpFilters::AiProtocolManager::LLMProtocol protocol,
                         const nlohmann::json& json,
                         const HttpFilters::AiProtocolManager::LlmBindingRead* arrived);

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
