#pragma once

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"

#include "nlohmann/json.hpp" // IWYU pragma: keep

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

struct HttpRequest {
  std::string url;
  std::string method;
  nlohmann::json body;
};

// Builds an HttpRequest from `http_rule` and `arguments` from the JSON-RPC request body.
absl::StatusOr<HttpRequest> buildHttpRequest(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule& http_rule,
    const nlohmann::json& arguments);

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
