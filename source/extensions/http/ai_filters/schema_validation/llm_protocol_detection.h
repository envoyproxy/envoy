#pragma once

#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace SchemaValidation {

// The API a request's path names, matched as a suffix so a gateway prefix does not hide it, then
// the `anthropic-version` header.
HttpFilters::AiProtocolManager::LLMProtocol
detectFromHeaders(const Http::RequestHeaderMap& headers);

// The API a payload's shape names. Chat Completions and Anthropic Messages share `messages[]`, so
// markers of both, or of neither, detect nothing.
HttpFilters::AiProtocolManager::LLMProtocol detectFromPayload(const nlohmann::json& json);

} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
