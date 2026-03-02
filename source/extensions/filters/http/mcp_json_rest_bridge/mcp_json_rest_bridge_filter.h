#pragma once

#include <memory>

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "nlohmann/json.hpp" // IWYU pragma: keep

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

/**
 * Configuration for the MCP JSON REST Bridge filter.
 */
class McpJsonRestBridgeFilterConfig : public Router::RouteSpecificFilterConfig,
                                      public Logger::Loggable<Logger::Id::config> {
public:
  explicit McpJsonRestBridgeFilterConfig(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
          proto_config);

  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
  getHttpRule(absl::string_view tool_name) const;

private:
  absl::flat_hash_map<std::string,
                      envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
      tool_to_http_rule_;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config_;
};

using McpJsonRestBridgeFilterConfigSharedPtr = std::shared_ptr<McpJsonRestBridgeFilterConfig>;

/**
 * MCP JSON REST Bridge proxy implementation.
 */
class McpJsonRestBridgeFilter : public Http::PassThroughFilter,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  explicit McpJsonRestBridgeFilter(McpJsonRestBridgeFilterConfigSharedPtr config)
      : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  // Handles "method" field in the MCP request.
  void handleMcpMethod(const nlohmann::json& json_rpc);

  // Sends MCP error response.
  void sendErrorResponse(Http::Code response_code, absl::string_view response_code_details,
                         absl::string_view response_body);

  // Validates the "id" and "method" fields of a JSON-RPC request.
  // It sends local error response and return an error status if the validation
  // fails. Otherwise, it returns OK status.
  absl::Status validateJsonRpcIdAndMethod(const nlohmann::json& json_rpc);

  enum class McpOperation {
    Unspecified = 0,
    // Received the "/mcp" URL but has not parsed the request body yet.
    Undecided = 1,
    // InitializeRequest in the init handshake flow.
    Initialization = 2,
    // InitializedNotification in the init handshake flow.
    InitializationAck = 3,
    // Clients send a tools/list request to discover available tools.
    ToolsList = 4,
    // Clients send a tools/call request to invoke a tool.
    ToolsCall = 5,
    // MCP operation failed.
    OperationFailed = 6,
  };
  McpOperation mcp_operation_ = McpOperation::Unspecified;
  absl::optional<int> session_id_;
  std::string server_name_;
  Buffer::OwnedImpl request_body_;

  McpJsonRestBridgeFilterConfigSharedPtr config_;
};

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
