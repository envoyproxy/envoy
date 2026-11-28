#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/headers.h"
#include "source/extensions/filters/common/mcp/constants.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/bridge_status.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/sse_response_extractor.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
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

struct McpParamHeaderMapping {
  Http::LowerCaseString header_name;
  std::vector<std::string> property_path;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpParamHeader::Type type;
};

// Cache hints returned in locally served tools/list responses.
struct ToolListCacheConfig {
  uint64_t ttl_ms = 0;
  absl::string_view scope =
      Envoy::Extensions::Filters::Common::Mcp::McpConstants::CACHE_SCOPE_PUBLIC;
};

/**
 * Configuration for the MCP JSON REST Bridge filter.
 */
class McpJsonRestBridgeFilterConfig : public Logger::Loggable<Logger::Id::config> {
public:
  static absl::StatusOr<std::shared_ptr<McpJsonRestBridgeFilterConfig>>
  create(const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
             proto_config);

  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
  getHttpRule(absl::string_view tool_name, absl::string_view host, absl::string_view path) const;
  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
  getToolsListHttpRule(absl::string_view host, absl::string_view path) const;

  const std::string& fallbackProtocolVersion() const { return fallback_protocol_version_; }
  const std::string& maxSupportedProtocolVersion() const { return max_supported_protocol_version_; }

  // The request body limit `proto_config` sets, or the default; 0 for no limit.
  static uint32_t maxRequestBodySize(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
          proto_config);

  uint32_t maxRequestBodySize() const { return max_request_body_size_; }
  uint32_t maxResponseBodySize() const { return max_response_body_size_; }

  uint64_t serverDiscoveryCacheTtlMs() const { return server_discovery_cache_ttl_ms_; }
  absl::string_view serverDiscoveryCacheScope() const { return server_discovery_cache_scope_; }

  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge::RequestStorageMode
  requestStorageMode() const {
    return proto_config_.request_storage_mode();
  }

  // Returns tools to serve a local tools/list response, filtered by host and path.
  std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
  toolListLocalTools(absl::string_view host, absl::string_view path) const;

  // Returns whether local serving of tools/list is configured for the matching endpoint.
  bool toolListLocal(absl::string_view host, absl::string_view path) const;

  // Returns the endpoint-level tools/list cache config for the complete merged response by
  // selecting the first matching tool_list_local tier (in (host, path) > (host, "") > ("", path) >
  // ("", "") precedence order) that explicitly sets cache_config. Tiers with tool_list_local that
  // omit cache_config inherit from the next matching tier; returns defaults if no matching tier
  // sets cache_config.
  ToolListCacheConfig toolListLocalCacheConfig(absl::string_view host,
                                               absl::string_view path) const;

  // Returns whether there is a configured endpoint matching the host and path.
  bool hasEndpoint(absl::string_view host, absl::string_view path) const;

  bool textContentStreamingEnabled(absl::string_view tool_name, absl::string_view host,
                                   absl::string_view path) const;

  const std::vector<McpParamHeaderMapping>& mcpParamHeaders(absl::string_view tool_name,
                                                            absl::string_view host,
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

  bool perRouteOnly() const { return proto_config_.per_route_only(); }

private:
  explicit McpJsonRestBridgeFilterConfig(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
          proto_config);

  absl::Status initialize();

  struct ToolEntry {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule http_rule;
    bool text_content_streaming_enabled;
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig* tool_proto;
    std::vector<McpParamHeaderMapping> mcp_param_headers;
  };
  struct EndpointConfig {
    absl::flat_hash_map<std::string, ToolEntry> tool_entries;
    std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
        tools;
    std::optional<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
        tool_list_http_rule;
    bool tool_list_local = false;
    std::optional<ToolListCacheConfig> tool_list_cache_config;
  };
  absl::flat_hash_map<EndpointKey, EndpointConfig> endpoint_configs_;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config_;
  std::string fallback_protocol_version_;
  std::string max_supported_protocol_version_;
  uint32_t max_request_body_size_;
  uint32_t max_response_body_size_;
  uint64_t server_discovery_cache_ttl_ms_;

  absl::string_view server_discovery_cache_scope_;
  bool clear_route_cache_;
};

class McpJsonRestBridgePerRouteConfig : public Router::RouteSpecificFilterConfig,
                                        public Logger::Loggable<Logger::Id::config> {
public:
  static absl::StatusOr<std::shared_ptr<McpJsonRestBridgePerRouteConfig>> create(
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

  // Returns the endpoint-level tools/list cache config for the complete merged response by
  // selecting the first matching tool_list_local tier (in (host, path) > (host, "") > ("", path) >
  // ("", "") precedence order) that explicitly sets cache_config. Tiers with tool_list_local that
  // omit cache_config inherit from the next matching tier; returns defaults if no matching tier
  // sets cache_config.
  ToolListCacheConfig toolListLocalCacheConfig(absl::string_view host,
                                               absl::string_view path) const;

  // Returns whether there is a configured endpoint matching the host and path.
  bool hasEndpoint(absl::string_view host, absl::string_view path) const;

  bool textContentStreamingEnabled(absl::string_view tool_name, absl::string_view host,
                                   absl::string_view path) const;

  const std::vector<McpParamHeaderMapping>& mcpParamHeaders(absl::string_view tool_name,
                                                            absl::string_view host,
                                                            absl::string_view path) const;

private:
  explicit McpJsonRestBridgePerRouteConfig(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute&
          proto_config);

  absl::Status initialize();

  struct ToolEntry {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule http_rule;
    bool text_content_streaming_enabled;
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig* tool_proto;
    std::vector<McpParamHeaderMapping> mcp_param_headers;
  };
  struct EndpointConfig {
    absl::flat_hash_map<std::string, ToolEntry> tool_entries;
    std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
        tools;
    std::optional<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
        tool_list_http_rule;
    bool tool_list_local = false;
    std::optional<ToolListCacheConfig> tool_list_cache_config;
  };
  absl::flat_hash_map<EndpointKey, EndpointConfig> endpoint_configs_;
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute
      proto_config_;
};

using McpJsonRestBridgeFilterConfigSharedPtr = std::shared_ptr<McpJsonRestBridgeFilterConfig>;
using McpJsonRestBridgePerRouteConfigSharedPtr = std::shared_ptr<McpJsonRestBridgePerRouteConfig>;

// What a local reply of the bridge takes the place of.
enum class LocalReplyFor {
  // The request.
  Request,
  // A Buffered response, whose headers are still held.
  BufferedResponse,
  // A Streaming response, whose headers have gone downstream.
  StreamingResponse,
};

// The stream a bridge runs on, as the filter hosting it provides it.
class BridgeCallbacks {
public:
  virtual ~BridgeCallbacks() = default;

  virtual void sendLocalReply(Http::Code code, absl::string_view body,
                              std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                              Grpc::Status::GrpcStatus grpc_status, absl::string_view details,
                              LocalReplyFor reply_for) PURE;
  virtual void clearRouteCache() PURE;
  virtual StreamInfo::StreamInfo& streamInfo() PURE;
  // The hosting filter's configured name, which is the dynamic metadata namespace.
  virtual absl::string_view filterConfigName() const PURE;
  // For ENVOY_STREAM_LOG.
  virtual OptRef<const Network::Connection> connection() PURE;
  virtual uint64_t streamId() const PURE;
};

/**
 * Translates one MCP JSON-RPC exchange to and from a JSON REST backend. It is the logic the
 * HTTP filter and the AI filter share: the filter hosting it supplies the stream through
 * BridgeCallbacks, buffers what the bridge asks for, and forwards what the bridge returns.
 */
class McpJsonRestBridge : public Logger::Loggable<Logger::Id::filter> {
public:
  McpJsonRestBridge(McpJsonRestBridgeFilterConfigSharedPtr config, BridgeCallbacks& callbacks)
      : sse_response_extractor_(config->maxResponseBodySize()), callbacks_(callbacks),
        config_(std::move(config)) {}

  enum class RequestStatus {
    // Not a request the bridge handles: forward it as received.
    PassThrough,
    // The bridge replied locally.
    Replied,
    // Hold the request for its body.
    NeedsBody,
    // Forward the request with the body the bridge returned, null for an empty one. The host
    // frames the body, as with setContentLength().
    Forward,
    // Forward the request without a body.
    ForwardNoBody,
  };

  // Starts a request. `end_stream` is whether it ended on its headers.
  RequestStatus onRequestHeaders(const Http::RequestHeaderMap& request_headers, bool end_stream,
                                 const McpJsonRestBridgePerRouteConfig* per_route_config);

  // Returns whether a request body of `size` bytes is within the limit; replies locally if not.
  bool onRequestBodySize(uint64_t size);

  // Replies locally to a request whose body is more than the bridge can take.
  void onRequestBodyTooLarge();

  // Replies locally to a request whose body is not JSON.
  void onRequestBodyInvalid();

  // Handles the JSON-RPC request. When it is forwarded, `request_headers` are rewritten into
  // those of the backend request, and on Forward `body` is its body.
  RequestStatus onRequestBody(const nlohmann::json& json_rpc,
                              Http::RequestHeaderMapOptRef request_headers,
                              const McpJsonRestBridgePerRouteConfig* per_route_config,
                              nlohmann::json& body);

  // Returns the JSON-RPC error response for `error`, the body of the local reply a failed
  // operation was answered with; nullopt for any other reply.
  std::optional<std::string> jsonRpcError(absl::string_view error) const;

  enum class ResponseStatus {
    // Not a response the bridge handles: forward it as received.
    PassThrough,
    // The response ended on its headers: forward the body the bridge returned.
    Complete,
    // Forward the headers, and each body chunk through encodeStreamingData().
    Streaming,
    // Hold the headers, and buffer the body for encodeJsonRpcData().
    Buffered,
  };

  // Starts a response, rewriting its headers unless it is Buffered. `body` is the body of a
  // Complete response.
  ResponseStatus onResponseHeaders(Http::ResponseHeaderMap& response_headers, bool end_stream,
                                   std::string& body);

  // How the body of the response is handled: PassThrough, Streaming or Buffered.
  ResponseStatus responseStatus() const;

  // Translates one chunk of a Streaming response in place. Returns false after replying locally
  // to a chunk it cannot translate.
  bool encodeStreamingData(Buffer::Instance& data, bool end_stream);

  // Returns what closes a Streaming response that ended without an end_stream chunk.
  std::string encodeStreamingEnd();

  // Returns whether `size` buffered bytes of a Buffered response are within the limit; replies
  // locally if not.
  bool onResponseBodySize(uint64_t size, Http::ResponseHeaderMapOptRef response_headers);

  // Returns the JSON-RPC response for the Buffered `body`, and rewrites the headers for it.
  std::string encodeJsonRpcData(Buffer::Instance& body,
                                Http::ResponseHeaderMapOptRef response_headers);

  // Whether the bridge closes the response body ahead of its trailers.
  bool handlesResponseTrailers() const {
    return mcp_operation_ == McpOperation::ToolsList || mcp_operation_ == McpOperation::ToolsCall;
  }

  // Sets content-length for a body of `size` bytes, unless the message is chunked.
  template <class HeaderMap> static void setContentLength(HeaderMap& headers, uint64_t size) {
    const auto transfer_encoding = headers.TransferEncoding();
    const bool is_chunked =
        transfer_encoding != nullptr &&
        absl::EqualsIgnoreCase(transfer_encoding->value().getStringView(),
                               Http::Headers::get().TransferEncodingValues.Chunked);
    if (is_chunked) {
      headers.removeContentLength();
    } else {
      headers.setContentLength(size);
    }
  }

private:
  // Validates the "MCP-Protocol-Version" request header against the JSON-RPC request body.
  // Sends a local error response and returns an error status if validation fails.
  absl::Status validateMcpProtocolVersionHeader(const nlohmann::json& json_rpc,
                                                absl::string_view method,
                                                Http::RequestHeaderMapOptConstRef request_headers);

  // Validates that a stateless (2026-07-28) request carries the required
  // params._meta["io.modelcontextprotocol/clientCapabilities"] field and that it is a JSON object.
  // Individual capability contents are not validated. Must only be called for stateless requests
  // after validateMcpProtocolVersionHeader() succeeds, which guarantees that params._meta is an
  // object. Returns true when the field is valid. Otherwise sends a local error response and
  // returns false.
  bool validateClientCapabilitiesMeta(const nlohmann::json& json_rpc, absl::string_view method);

  // Validates that the "Mcp-Method" request header is present and matches the JSON-RPC "method"
  // field. Returns true when the header is valid. Otherwise sends a local error response and
  // returns false.
  bool validateMcpMethodHeader(const nlohmann::json& json_rpc, absl::string_view method,
                               Http::RequestHeaderMapOptRef request_headers);

  // Validates that the "Mcp-Name" request header is present and matches the JSON-RPC
  // "params.name" field for a tools/call request.
  bool validateMcpNameHeader(const nlohmann::json& json_rpc, absl::string_view method,
                             Http::RequestHeaderMapOptRef request_headers);

  // Validates the configured "Mcp-Param-{name}" request headers of a resolved tool against the
  // tools/call "arguments". Request headers without a configured mapping are not inspected.
  // Returns true when every mapping is valid. Otherwise sends a local HeaderMismatch error response
  // and returns false.
  bool validateMcpParamHeaders(const std::vector<McpParamHeaderMapping>& mappings,
                               const nlohmann::json& arguments, const nlohmann::json& params,
                               Http::RequestHeaderMapOptRef request_headers);

  // Serves a local tools/list response using tools' ToolsListSpecificConfig.
  void serveToolsListLocal(
      const nlohmann::json& json_rpc,
      const std::vector<
          const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>& tools,
      const ToolListCacheConfig& cache_config);

  // Maps the tool call request to the backend API.
  RequestStatus mapMcpToolToApiBackend(const nlohmann::json& json_rpc,
                                       Http::RequestHeaderMapOptRef request_headers,
                                       const McpJsonRestBridgePerRouteConfig* per_route_config,
                                       nlohmann::json& body);

  // Handles decoding errors: sets dynamic metadata and sends a local reply.
  // IMPORTANT PROTOCOL RULE:
  // 1. For JSON-RPC application/protocol errors in the request payload (-32600, -32601, -32602),
  //    MUST use Http::Code::OK (200). Many MCP SDK clients inspect HTTP status before JSON-RPC
  //    decoding and will fail with a transport exception on non-200 responses, discarding the
  //    structured JSON-RPC error code/message. The only exceptions are stateless MCP 2026-07-28
  //    requests, which MUST use 404 Not Found for -32601 (see below) and 400 Bad Request for
  //    -32602 when required params._meta fields are missing or invalid.
  // 2. Only use non-200 HTTP codes (400, 401, 403, 404, 405, 413) in the following cases:
  //    - Transport-level or framing syntax failures (generated locally by this filter):
  //      * 405 Method Not Allowed (non-POST request)
  //      * 413 Payload Too Large (exceeding maxRequestBodySize)
  //      * 400 Bad Request for malformed JSON syntax (-32700 parse error)
  //      * 400 Bad Request when an MCP standard request header fails validation (-32020 header
  //        mismatch), which is an HTTP-level error rather than a JSON-RPC payload error
  //    - Unsupported methods in stateless MCP 2026-07-28 requests (generated by this filter):
  //      * 404 Not Found with -32601 (Method not found), as required by the Streamable HTTP
  //        transport:
  //        https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http
  //    - Rejected JSON-RPC notifications (generated by this filter). A notification has no JSON-RPC
  //      response, so the Streamable HTTP transport requires an HTTP error status. 200 OK is
  //      replaced with 400 Bad Request for them.
  //    - Authorization errors preserved from upstream (401 Unauthorized, 403 Forbidden):
  //      * 401 and 403 from upstream are preserved as non-200 HTTP responses as required by the MCP
  //        authorization spec:
  //        https://modelcontextprotocol.io/specification/2025-11-25/basic/authorization#error-handling
  //      * Note on 400 Bad Request: Although the MCP authorization spec also includes HTTP 400 for
  //        certain authorization errors, we assume authorization checks (e.g., OAuth token
  //        validation) occur in filters before MCP transcoding. Therefore, any 400 error from the
  //        REST backend is assumed to be an API error rather than an authorization error, so the
  //        MCP transcoder transforms the REST 400 error into a standard JSON-RPC error (with HTTP
  //        200 OK) and does not preserve it as an authorization error.
  void sendErrorResponse(
      Http::Code response_code, BridgeStatus status, absl::string_view response_body,
      std::function<void(Http::ResponseHeaderMap&)> modify_headers = nullptr,
      absl::string_view method = "", const nlohmann::json& params = nlohmann::json::object(),
      Grpc::Status::GrpcStatus grpc_status = Grpc::Status::WellKnownGrpcStatus::Internal);

  // Validates the "id" and "method" fields of a JSON-RPC request.
  // It sends local error response and return an error status if the validation
  // fails. Otherwise, it returns OK status. A JSON-RPC notification, i.e. a message without an "id"
  // member whose method is an MCP notification, does not need an ID and sets is_notification_.
  absl::Status validateJsonRpcIdAndMethod(const nlohmann::json& json_rpc);

  // Sets dynamic metadata for the filter based on the MCP request method and parameters.
  void setParsingMetadata(absl::string_view method, const nlohmann::json& params);
  void setResponseMetadata(BridgeStatus status,
                           std::optional<uint64_t> response_code = std::nullopt);
  void setDynamicMetadata();

  // Builds streaming_json_prefix_ and streaming_json_suffix_ for the tools/call streaming path.
  void buildStreamingPrefixAndSuffix(bool is_error);

  // Processes an SSE response chunk and returns the serialized JSON event payloads.
  absl::StatusOr<std::string> processSseResponse(absl::string_view chunk, bool end_stream);

  // Prepares the escaped/formatted payload string for streaming.
  absl::StatusOr<std::string> prepareStreamingPayload(absl::string_view chunk, bool end_stream);

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
    // Clients send a server/discover request that is handled locally.
    ServerDiscover = 8,
  };
  McpOperation mcp_operation_ = McpOperation::Unspecified;
  std::optional<nlohmann::json> session_id_;
  bool is_stateless_request_{false};
  bool is_notification_{false};
  std::string server_name_;
  std::string path_;

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

  // Whether the response is SSE.
  bool is_sse_response_ = false;
  bool is_first_sse_event_ = true;
  SseResponseExtractor sse_response_extractor_;

  BridgeCallbacks& callbacks_;
  McpJsonRestBridgeFilterConfigSharedPtr config_;
};

/**
 * MCP JSON REST Bridge HTTP filter: hosts the bridge on the HTTP filter chain.
 */
class McpJsonRestBridgeFilter : public Http::PassThroughFilter,
                                public BridgeCallbacks,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  explicit McpJsonRestBridgeFilter(McpJsonRestBridgeFilterConfigSharedPtr config)
      : bridge_(std::move(config), *this) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

  // BridgeCallbacks
  void sendLocalReply(Http::Code code, absl::string_view body,
                      std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                      Grpc::Status::GrpcStatus grpc_status, absl::string_view details,
                      LocalReplyFor reply_for) override;
  void clearRouteCache() override {
    if (decoder_callbacks_->downstreamCallbacks().has_value()) {
      decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    }
  }
  StreamInfo::StreamInfo& streamInfo() override { return decoder_callbacks_->streamInfo(); }
  absl::string_view filterConfigName() const override {
    return decoder_callbacks_->filterConfigName();
  }
  OptRef<const Network::Connection> connection() override {
    return decoder_callbacks_->connection();
  }
  uint64_t streamId() const override { return decoder_callbacks_->streamId(); }

private:
  McpJsonRestBridge bridge_;
  // Whether the bridge handles the request.
  bool bridged_{false};
  Buffer::OwnedImpl request_body_;
  Buffer::OwnedImpl response_body_;
};

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
