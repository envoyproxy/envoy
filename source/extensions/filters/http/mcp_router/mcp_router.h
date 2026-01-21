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
  ResourcesList,
  ResourcesRead,
  ResourcesSubscribe,
  ResourcesUnsubscribe,
  PromptsList,
  PromptsGet,
  Ping,
  NotificationInitialized,
};

McpMethod parseMethodString(absl::string_view method_str);

/** Response received from a backend MCP server. */
struct BackendResponse {
  std::string backend_name;
  bool success{false};
  uint64_t status_code{0};
  std::string body;
  std::string session_id;
  std::string error;
};

using AggregationCallback = std::function<void(std::vector<BackendResponse>)>;

/**
 * Callbacks for handling async HTTP responses from backend MCP servers.
 * Accumulates response data and invokes completion callback when stream ends.
 */
class BackendStreamCallbacks : public Http::AsyncClient::StreamCallbacks,
                               public std::enable_shared_from_this<BackendStreamCallbacks>,
                               public Logger::Loggable<Logger::Id::filter> {
public:
  BackendStreamCallbacks(const std::string& backend_name,
                         std::function<void(BackendResponse)> on_complete);

  // AsyncClient::StreamCallbacks
  void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void onComplete() override;
  void onReset() override;

private:
  void complete();

  std::string backend_name_;
  std::function<void(BackendResponse)> on_complete_;
  BackendResponse response_;
  bool completed_{false};
};

/**
 * HTTP filter that routes MCP requests to one or more backend servers.
 * Supports fanout to multiple backends for initialize/tools-list and
 * single-backend routing for tools-call based on tool name prefix.
 */
class McpRouterFilter : public Http::StreamDecoderFilter,
                        public Logger::Loggable<Logger::Id::filter> {
public:
  explicit McpRouterFilter(McpRouterConfigSharedPtr config);
  ~McpRouterFilter() override;

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
  std::pair<std::string, std::string> parseResourceUri(const std::string& uri);
  std::pair<std::string, std::string> parsePromptName(const std::string& prefixed_name);
  // Rewrites the tool name in the buffer. Returns the size delta (new_size - old_size).
  ssize_t rewriteToolCallBody(Buffer::Instance& buffer);
  // Rewrites the resource URI in the buffer. Returns the size delta (new_size - old_size).
  ssize_t rewriteResourceUriBody(Buffer::Instance& buffer);
  // Rewrites the prompt name in the buffer. Returns the size delta (new_size - old_size).
  ssize_t rewritePromptsGetBody(Buffer::Instance& buffer);
  // Helper to replace content at a position in the buffer, and return the delta.
  ssize_t rewriteAtPosition(Buffer::Instance& buffer, ssize_t pos, const std::string& search_str,
                            const std::string& replacement);
  // Helper for resource methods that route to a single backend based on URI.
  void handleSingleBackendResourceMethod(absl::string_view method_name);

  // Initialization handlers - set up connections, don't send body.
  void handleInitialize();
  void handleToolsList();
  void handleToolsCall();
  void handleResourcesList();
  void handleResourcesRead();
  void handleResourcesSubscribe();
  void handleResourcesUnsubscribe();
  void handlePromptsList();
  void handlePromptsGet();
  void handlePing();
  void handleNotificationInitialized();

  // Aggregation functions.
  std::string aggregateInitialize(const std::vector<BackendResponse>& responses);
  std::string aggregateToolsList(const std::vector<BackendResponse>& responses);
  std::string aggregateResourcesList(const std::vector<BackendResponse>& responses);
  std::string aggregatePromptsList(const std::vector<BackendResponse>& responses);

  // Initialize fanout connections.
  void initializeFanout(AggregationCallback callback);

  // Initialize single backend connection.
  void initializeSingleBackend(const McpBackendConfig& backend,
                               std::function<void(BackendResponse)> callback);

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
  std::string resource_uri_;         // Original resource URI (e.g., "time://path/to/resource")
  std::string rewritten_uri_;        // Rewritten URI for backend (e.g., "file://path/to/resource")
  std::string prompt_name_;          // Original prefixed prompt name (e.g., "time__greeting")
  std::string unprefixed_prompt_name_; // Unprefixed prompt name for backend (e.g., "greeting")
  bool needs_body_rewrite_{false};     // Whether tool/prompt name or URI rewriting is needed

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
