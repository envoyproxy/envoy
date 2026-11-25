#pragma once

#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace SchemaValidation {

// The API a request's path names, matched as a suffix so a gateway prefix does not hide it, else
// the one its payload's shape names. Chat Completions and Anthropic Messages share `messages[]`,
// so markers of both, or of neither, detect nothing. Nor does a path that names another endpoint
// of a provider, such as `/v1/messages/count_tokens`, whatever its payload.
HttpFilters::AiProtocolManager::LLMProtocol detectLlmProtocol(const Http::RequestHeaderMap& headers,
                                                              const nlohmann::json& json);

} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
