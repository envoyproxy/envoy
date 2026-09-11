#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/strings/string_view.h"
#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

struct RequestAttributes {
  HttpFilters::AiProtocolManager::ApiProtocol api_protocol{
      HttpFilters::AiProtocolManager::ApiProtocol::Unspecified};
  std::string model;
  std::optional<bool> stream;
  std::optional<uint64_t> max_output_tokens;
  std::optional<uint32_t> message_count;
  std::optional<uint32_t> tool_count;
  // Set when a present attribute was unusable and so reads as absent.
  bool malformed{false};
};

// `path` is the request :path, where Gemini names the model and the streaming operation.
RequestAttributes extractRequestAttributes(HttpFilters::AiProtocolManager::ApiProtocol protocol,
                                           const nlohmann::json& json, absl::string_view path);

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
