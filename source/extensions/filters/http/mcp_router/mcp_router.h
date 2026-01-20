#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/muxdemux.h"
#include "source/common/http/sse/sse_parser.h"
#include "source/extensions/filters/http/mcp_router/filter_config.h"
#include "source/extensions/filters/http/mcp_router/session_codec.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

namespace MetadataKeys {
constexpr absl::string_view kFilterNamespace = "envoy.filters.http.mcp";
} // namespace MetadataKeys

/** Enumeration of supported MCP protocol methods. */
enum class McpMethod {
  Unknown,
  Initialize,
  ToolsList,
  ToolsCall,
  Ping,
  NotificationInitialized,
};

McpMethod parseMethodString(absl::string_view method_str);

/** Content type of the backend response. */
enum class ResponseContentType {
  Unknown,
  Json,
  Sse,
};
/** Response received from a backend MCP server. */
struct BackendResponse {
  std::string backend_name;
  bool success{false};
  uint64_t status_code{0};
  std::string session_id;
  std::string error;

  // Content type determines how to interpret the response.
  ResponseContentType content_type{ResponseContentType::Unknown};

  // Response body (for both JSON and SSE responses).
  std::string body;

  // Cached extracted JSON-RPC body (populated during incremental SSE parsing).
  std::string extracted_jsonrpc;

  bool isJson() const { return content_type == ResponseContentType::Json; }
  bool isSse() const { return content_type == ResponseContentType::Sse; }

  // Get the JSON-RPC response body.
  // For JSON, returns body directly.
  // For SSE, parses all events and returns the data field with matching request ID.
  // If no request_id is provided, returns the first data field.
  std::string getJsonRpcBody(int64_t request_id = 0) const;
};

using AggregationCallback = std::function<void(std::vector<BackendResponse>)>;

// Forward declaration
class McpRouterFilter;

/**
 * Callbacks for handling async HTTP responses from backend MCP servers.
 * Accumulates response data and invokes completion callback when stream ends.
 *
 * SSE Support:
 * - Detects Content-Type: text/event-stream in onHeaders()
 * - Buffers SSE response body for pass-through (tools/call)
 * - Extracts first SSE event data during parsing for aggregation (tools/list)
 */
class BackendStreamCallbacks : public Http::AsyncClient::StreamCallbacks,
                               public std::enable_shared_from_this<BackendStreamCallbacks>,
                               public Logger::Loggable<Logger::Id::filter> {
public:
  /**
   * @param backend_name Name of the backend for logging.
   * @param on_complete Callback invoked when response is complete (aggregation mode).
   * @param request_id JSON-RPC request ID for SSE response matching.
   * @param aggregate_mode If true, complete early when valid SSE response is received.
   * @param parent Weak pointer to the parent filter for streaming SSE updates.
   * @param streaming_enabled If true, enables SSE streaming pass-through mode.
   */
  BackendStreamCallbacks(const std::string& backend_name,
                         std::function<void(BackendResponse)> on_complete, int64_t request_id = 0,
                         bool aggregate_mode = false, std::weak_ptr<McpRouterFilter> parent = {},
                         bool streaming_enabled = false);

  // AsyncClient::StreamCallbacks
  void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void onComplete() override;
  void onReset() override;

private:
  void complete();
  bool hasSseJsonRpcResponse();

  std::string backend_name_;
  std::function<void(BackendResponse)> on_complete_;
  int64_t request_id_{0};
  bool aggregate_mode_{false};
  std::weak_ptr<McpRouterFilter> parent_; // Safe handle to parent for streaming
  bool streaming_enabled_{false};         // Enable streaming pass-through
  bool streaming_started_{false};         // Track if streaming headers sent
  size_t parse_offset_{0};                // Track SSE parse position for incremental parsing
  bool found_response_{false};            // Cache result to avoid re-parsing
  BackendResponse response_;
  bool completed_{false};
};

/**
 * HTTP filter that routes MCP requests to one or more backend servers.
 * Supports fanout to multiple backends for initialize/tools-list and
 * single-backend routing for tools-call based on tool name prefix.
 *
 * SSE Support:
 * - tools/list: Aggregates SSE responses from multiple backends into single JSON response
 * - tools/call: Passes through SSE response from single backend
 */
class McpRouterFilter : public Http::StreamDecoderFilter,
                        public Logger::Loggable<Logger::Id::filter>,
                        public std::enable_shared_from_this<McpRouterFilter> {
public:
  explicit McpRouterFilter(McpRouterConfigSharedPtr config);
  ~McpRouterFilter() override;

  // Streaming methods called by BackendStreamCallbacks safely via weak_ptr.
  void pushSseHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream);
  void pushSseData(Buffer::Instance& data, bool end_stream);
  void onStreamingError(absl::string_view error);
  void onStreamingComplete();

  void onDestroy() override;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

private:
  bool readMetadataFromMcpFilter();
  bool decodeAndParseSession();
  absl::StatusOr<std::string> getAuthenticatedSubject();
  bool validateSubjectIfRequired();

  std::pair<std::string, std::string> parseToolName(const std::string& prefixed_name);
  // Rewrites the tool name in the buffer. Returns the size delta (new_size - old_size).
  ssize_t rewriteToolCallBody(Buffer::Instance& buffer);
  // Helper to replace content at a position in the buffer, and return the delta.
  ssize_t rewriteAtPosition(Buffer::Instance& buffer, ssize_t pos, const std::string& search_str,
                            const std::string& replacement);

  // Initialization handlers - set up connections, don't send body
  void handleInitialize();
  void handleToolsList();
  void handleToolsCall();
  void handlePing();
  void handleNotificationInitialized();

  // Aggregation functions
  std::string aggregateInitialize(const std::vector<BackendResponse>& responses);
  std::string aggregateToolsList(const std::vector<BackendResponse>& responses);

  // SSE-aware response extraction: extracts JSON-RPC from SSE events if needed.
  std::string extractJsonRpcFromResponse(const BackendResponse& response);

  // Initialize fanout connections.
  void initializeFanout(AggregationCallback callback);

  // Initialize single backend connection.
  void initializeSingleBackend(const McpBackendConfig& backend,
                               std::function<void(BackendResponse)> callback);

  // Initialize single backend connection with optional streaming mode for SSE.
  void initializeSingleBackend(const McpBackendConfig& backend,
                               std::function<void(BackendResponse)> callback,
                               bool streaming_enabled);

  // Stream data to established connection(s)
  void streamData(Buffer::Instance& data, bool end_stream);

  void sendJsonResponse(const std::string& body, const std::string& session_id = "");
  void sendAccepted();
  void sendHttpError(uint64_t status_code, const std::string& message);

  Http::RequestHeaderMapPtr createUpstreamHeaders(const McpBackendConfig& backend,
                                                  const std::string& backend_session_id = "");

  McpRouterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};

  Http::RequestHeaderMap* request_headers_{};
  Buffer::OwnedImpl request_body_;

  int64_t request_id_{0};
  McpMethod method_{McpMethod::Unknown};
  std::string tool_name_;            // Original prefixed tool name (e.g., "time__get_current_time")
  std::string unprefixed_tool_name_; // Unprefixed tool name for backend (e.g., "get_current_time")
  bool needs_body_rewrite_{false};   // Whether tool name rewriting is needed

  std::string route_name_{"default"};
  std::string session_subject_;
  std::string encoded_session_id_;
  absl::flat_hash_map<std::string, std::string> backend_sessions_;

  // MuxDemux for all backend operations (fanout and single-backend)
  std::shared_ptr<Http::MuxDemux> muxdemux_;
  std::unique_ptr<Http::MultiStream> multistream_;
  std::vector<std::shared_ptr<BackendStreamCallbacks>> stream_callbacks_;

  // Store headers to keep them alive for the duration of the stream
  // AsyncStreamImpl stores only a pointer to headers, so we must keep them alive
  std::vector<Http::RequestHeaderMapPtr> upstream_headers_;

  // Aggregation state
  std::shared_ptr<std::vector<BackendResponse>> pending_responses_;
  std::shared_ptr<size_t> response_count_;
  AggregationCallback aggregation_callback_;
  std::function<void(BackendResponse)> single_backend_callback_;

  bool initialized_{false}; // Track if fanout/backend has been initialized
};

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
