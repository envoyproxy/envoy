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

// What a request asks for. `malformed` marks a known attribute that was present
// but unusable and therefore reads as absent.
struct RequestAttributes {
  HttpFilters::AiProtocolManager::ApiProtocol api_protocol{
      HttpFilters::AiProtocolManager::ApiProtocol::Unspecified};
  std::string model;
  std::optional<bool> stream;
  std::optional<uint64_t> max_output_tokens;
  std::optional<uint32_t> message_count;
  std::optional<uint32_t> tool_count;
  bool malformed{false};
};

// Reads `json` as a `protocol` request. `path` is the request :path, which is
// where Gemini names the model and the streaming operation.
RequestAttributes extractRequestAttributes(HttpFilters::AiProtocolManager::ApiProtocol protocol,
                                           const nlohmann::json& json, absl::string_view path);

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
