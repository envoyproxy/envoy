#pragma once

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"

#include "absl/container/flat_hash_set.h"
#include "nlohmann/json.hpp" // IWYU pragma: keep

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

// Percent-encode all printable ASCII characters in URL query parameters except for
// `[-_./0-9a-zA-Z]`, per the Google API path template syntax:
// https://cloud.google.com/service-infrastructure/docs/service-management/reference/rpc/google.api#path-template-syntax
inline constexpr absl::string_view ReservedChars = R"( !"#$%&'()*+,:;<=>?@[\]^`{|}~)";

struct HttpRequest {
  std::string url;
  std::string method;
  nlohmann::json body;
};

// Builds an HttpRequest from `http_rule` and `arguments` from the JSON-RPC request body.
absl::StatusOr<HttpRequest> buildHttpRequest(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule& http_rule,
    const nlohmann::json& arguments);

// Constructs a base URL by replacing template variables with values from the arguments.
// Exposed for testing.
absl::StatusOr<std::string> constructBaseUrl(absl::string_view pattern,
                                             const absl::flat_hash_set<std::string>& templates,
                                             const nlohmann::json& arguments);

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
