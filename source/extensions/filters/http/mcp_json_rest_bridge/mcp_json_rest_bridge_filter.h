#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp" // IWYU pragma: keep

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

inline constexpr char FilterName[] = "envoy.filters.http.mcp_json_rest_bridge";

struct EndpointKey {
  std::string host;
  std::string path;

  bool operator==(const EndpointKey& other) const {
    return host == other.host && path == other.path;
  }

  template <typename H> friend H AbslHashValue(H h, const EndpointKey& k) {
    return H::combine(std::move(h), k.host, k.path);
  }
};

enum class BridgeStatus {
  Ok,
  RequestNotPost,
  RequestTooLarge,
  RequestParseError,
  RequestUnsupportedProtocolVersion,
  RequestInitializeNotValid,
  RequestMethodNotSupported,
  RequestMethodNotFound,
  RequestMethodNotString,
  RequestIdNotFound,
  RequestToolParamsNotFound,
  RequestToolNameNotFound,
  RequestUnknownTool,
  RequestToolArgumentsInvalid,
  RequestToolTranscodingFailure,
  RequestPassthrough,
  ResponseTooLarge,
  ResponseInvalidUtf8,
  ResponseBackendError,
  ResponseParseError,
};

absl::string_view bridgeStatusToString(BridgeStatus status);

namespace BridgeStatusValues {
inline constexpr absl::string_view STATUS = "status";
inline constexpr absl::string_view OK = "mcp_json_rest_bridge_ok";
inline constexpr absl::string_view REQUEST_NOT_POST = "mcp_json_rest_bridge_request_not_post";
inline constexpr absl::string_view REQUEST_TOO_LARGE = "mcp_json_rest_bridge_request_too_large";
inline constexpr absl::string_view REQUEST_PARSE_ERROR =
    "mcp_json_rest_bridge_request_failed_to_parse_json_rpc";
inline constexpr absl::string_view REQUEST_UNSUPPORTED_PROTOCOL_VERSION =
    "mcp_json_rest_bridge_request_unsupported_protocol_version";
inline constexpr absl::string_view REQUEST_INITIALIZE_NOT_VALID =
    "mcp_json_rest_bridge_request_initialize_request_not_valid";
inline constexpr absl::string_view REQUEST_METHOD_NOT_SUPPORTED =
    "mcp_json_rest_bridge_request_method_not_supported";
inline constexpr absl::string_view REQUEST_METHOD_NOT_FOUND =
    "mcp_json_rest_bridge_request_method_not_found";
inline constexpr absl::string_view REQUEST_METHOD_NOT_STRING =
    "mcp_json_rest_bridge_request_method_not_string";
inline constexpr absl::string_view REQUEST_ID_NOT_FOUND =
    "mcp_json_rest_bridge_request_id_not_found";
inline constexpr absl::string_view REQUEST_TOOL_PARAMS_NOT_FOUND =
    "mcp_json_rest_bridge_request_tool_params_not_found";
inline constexpr absl::string_view REQUEST_TOOL_NAME_NOT_FOUND =
    "mcp_json_rest_bridge_request_tool_name_not_found";
inline constexpr absl::string_view REQUEST_UNKNOWN_TOOL =
    "mcp_json_rest_bridge_request_unknown_tool";
inline constexpr absl::string_view REQUEST_TOOL_ARGUMENTS_INVALID =
    "mcp_json_rest_bridge_request_tool_arguments_invalid";
inline constexpr absl::string_view REQUEST_TOOL_TRANSCODING_FAILURE =
    "mcp_json_rest_bridge_request_tool_transcoding_failure";
inline constexpr absl::string_view REQUEST_PASSTHROUGH = "mcp_json_rest_bridge_request_passthrough";
inline constexpr absl::string_view RESPONSE_TOO_LARGE = "mcp_json_rest_bridge_response_too_large";
inline constexpr absl::string_view RESPONSE_INVALID_UTF8 =
    "mcp_json_rest_bridge_response_invalid_utf8";
inline constexpr absl::string_view RESPONSE_BACKEND_ERROR =
    "mcp_json_rest_bridge_response_backend_error";
inline constexpr absl::string_view RESPONSE_PARSE_ERROR =
    "mcp_json_rest_bridge_response_failed_to_parse_json";
} // namespace BridgeStatusValues

/**
 * Configuration for the MCP JSON REST Bridge filter.
 */
class McpJsonRestBridgeFilterConfig : public Logger::Loggable<Logger::Id::config> {
public:
  explicit McpJsonRestBridgeFilterConfig(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
          proto_config);

  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
  getHttpRule(absl::string_view tool_name, absl::string_view host, absl::string_view path) const;
  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
  getToolsListHttpRule(absl::string_view host, absl::string_view path) const;

  const std::string& fallbackProtocolVersion() const { return fallback_protocol_version_; }

  uint32_t maxRequestBodySize() const { return max_request_body_size_; }
  uint32_t maxResponseBodySize() const { return max_response_body_size_; }

  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge::RequestStorageMode
  requestStorageMode() const {
    return proto_config_.request_storage_mode();
  }

  // Returns tools to serve a local tools/list response, filtered by host and path.
  std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
  toolListLocalTools(absl::string_view host, absl::string_view path) const;

  // Returns whether local serving of tools/list is configured for the matching endpoint.
  bool toolListLocal(absl::string_view host, absl::string_view path) const;

  // Returns whether there is a configured endpoint matching the host and path.
  bool hasEndpoint(absl::string_view host, absl::string_view path) const;

  bool textContentStreamingEnabled(absl::string_view tool_name, absl::string_view host,
                                   absl::string_view path) const;

  bool shouldStoreToDynamicMetadata() const {
    return proto_config_.request_storage_mode() ==
           envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge::
               DYNAMIC_METADATA;
  }

  bool traceContextExtraction() const { return proto_config_.has_trace_context_extraction(); }

  bool toolsListChanged() const { return proto_config_.tool_config().list_changed(); }

  const std::string& serverDescription() const { return proto_config_.server_info().description(); }

  bool clearRouteCache() const { return clear_route_cache_; }

private:
  struct ToolEntry {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule http_rule;
    bool text_content_streaming_enabled;
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig* tool_proto;
  };
  struct EndpointConfig {
    absl::flat_hash_map<std::string, ToolEntry> tool_entries;
    std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
        tools;
    std::optional<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
        tool_list_http_rule;
    bool tool_list_local = false;
  };
  absl::flat_hash_map<EndpointKey, EndpointConfig> endpoint_configs_;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config_;
  std::string fallback_protocol_version_;
  uint32_t max_request_body_size_;
  uint32_t max_response_body_size_;
  bool clear_route_cache_;
};

class McpJsonRestBridgePerRouteConfig : public Router::RouteSpecificFilterConfig,
                                        public Logger::Loggable<Logger::Id::config> {
public:
  explicit McpJsonRestBridgePerRouteConfig(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute&
          proto_config);

  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
  getHttpRule(absl::string_view tool_name, absl::string_view host, absl::string_view path) const;
  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
  getToolsListHttpRule(absl::string_view host, absl::string_view path) const;

  // Returns tools to serve a local tools/list response, filtered by host and path.
  std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
  toolListLocalTools(absl::string_view host, absl::string_view path) const;

  // Returns whether local serving of tools/list is configured for the matching endpoint.
  bool toolListLocal(absl::string_view host, absl::string_view path) const;

  // Returns whether there is a configured endpoint matching the host and path.
  bool hasEndpoint(absl::string_view host, absl::string_view path) const;

  bool textContentStreamingEnabled(absl::string_view tool_name, absl::string_view host,
                                   absl::string_view path) const;

private:
  struct ToolEntry {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule http_rule;
    bool text_content_streaming_enabled;
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig* tool_proto;
  };
  struct EndpointConfig {
    absl::flat_hash_map<std::string, ToolEntry> tool_entries;
    std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
        tools;
    std::optional<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
        tool_list_http_rule;
    bool tool_list_local = false;
  };
  absl::flat_hash_map<EndpointKey, EndpointConfig> endpoint_configs_;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute
      proto_config_;
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
  void handleMcpMethod(const nlohmann::json& json_rpc, Http::RequestHeaderMapOptRef request_headers,
                       const McpJsonRestBridgePerRouteConfig* per_route_config);

  // Serves a local tools/list response using tools' ToolsListSpecificConfig.
  void serveToolsListLocal(
      const nlohmann::json& json_rpc,
      const std::vector<
          const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>& tools);

  // Modifies the response from upstream into JSON-RPC response.
  void encodeJsonRpcData(Http::ResponseHeaderMapOptRef response_headers);

  // Maps the tool call request to the backend API.
  void mapMcpToolToApiBackend(const nlohmann::json& json_rpc,
                              const McpJsonRestBridgePerRouteConfig* per_route_config);

  // Handles decoding errors: sets dynamic metadata and sends a local reply.
  void sendErrorResponse(
      Http::Code response_code, BridgeStatus status, absl::string_view response_body,
      std::function<void(Http::ResponseHeaderMap&)> modify_headers = nullptr,
      absl::string_view method = "", const nlohmann::json& params = nlohmann::json::object(),
      Grpc::Status::GrpcStatus grpc_status = Grpc::Status::WellKnownGrpcStatus::Internal);

  // Validates the "id" and "method" fields of a JSON-RPC request.
  // It sends local error response and return an error status if the validation
  // fails. Otherwise, it returns OK status.
  absl::Status validateJsonRpcIdAndMethod(const nlohmann::json& json_rpc);

  // Sets dynamic metadata for the filter based on the MCP request method and parameters.
  void setParsingMetadata(absl::string_view method, const nlohmann::json& params);
  void setResponseMetadata(BridgeStatus status,
                           std::optional<uint64_t> response_code = std::nullopt);
  void setDynamicMetadata();

  // Builds streaming_json_prefix_ and streaming_json_suffix_ for the tools/call streaming path.
  void buildStreamingPrefixAndSuffix(bool is_error);

  enum class McpOperation {
    Unspecified = 0,
    // Received a configured MCP URL path but has not parsed the request body yet.
    Undecided = 1,
    // InitializeRequest in the init handshake flow.
    Initialization = 2,
    // InitializedNotification in the init handshake flow.
    InitializationAck = 3,
    // Clients send a tools/list request to discover available tools.
    ToolsList = 4,
    // Clients send a tools/list request that is handled locally.
    ToolsListLocal = 5,
    // Clients send a tools/call request to invoke a tool.
    ToolsCall = 6,
    // MCP operation failed.
    OperationFailed = 7,
  };
  McpOperation mcp_operation_ = McpOperation::Unspecified;
  std::optional<nlohmann::json> session_id_;
  std::string server_name_;
  std::string path_;
  Buffer::OwnedImpl request_body_;
  std::string request_body_str_;
  Buffer::OwnedImpl response_body_;
  std::string response_body_str_;

  // Per-request streaming flag, set during tool lookup in mapMcpToolToApiBackend.
  bool text_content_streaming_enabled_ = false;

  // Streaming state for text_content_streaming_enabled.
  // prefix/suffix are pre-built once in encodeHeaders; an empty prefix signals
  // that the non-streaming (buffered) path is active.
  std::string streaming_json_prefix_;
  std::string streaming_json_suffix_;
  bool is_first_streaming_chunk_ = true;

  BridgeStatus status_{BridgeStatus::Ok};
  std::string mcp_method_;
  Protobuf::Struct mcp_params_;
  bool has_params_ = false;
  std::optional<uint64_t> backend_response_code_;

  McpJsonRestBridgeFilterConfigSharedPtr config_;
};

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
