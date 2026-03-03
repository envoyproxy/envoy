#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/http/async_client.h"
#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

/** Content type of the backend response. */
enum class ResponseContentType {
  Unknown,
  Json,
  Sse,
};

/**
 * Classifies JSON-RPC messages in SSE events.
 */
enum class SseMessageType {
  Notification,  // method with no id (notifications/*)
  ServerRequest, // method with id (sampling/createMessage, roots/list)
  Response,      // has result or error with matching id
  Unknown
};

/**
 * Classifies a JSON-RPC message from SSE event data.
 * @param json_data The raw JSON-RPC message from SSE data field.
 * @param request_id The client request ID to match for Response classification.
 * @return The classified message type.
 */
SseMessageType classifyMessage(absl::string_view json_data, int64_t request_id);

/**
 * Detects the content type from a Content-Type header value.
 * Handles media type extraction (ignoring charset and other parameters).
 *
 * @param content_type_header The Content-Type header value.
 * @return The detected ResponseContentType.
 */
ResponseContentType detectContentType(absl::string_view content_type_header);

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

  // Returns the JSON-RPC body: extracted_jsonrpc if populated (SSE), otherwise body (JSON).
  const std::string& getJsonRpc() const {
    return extracted_jsonrpc.empty() ? body : extracted_jsonrpc;
  }
};

using AggregationCallback = std::function<void(std::vector<BackendResponse>)>;

/**
 * Interface for handling SSE streaming from backend to client.
 */
class SseStreamHandler {
public:
  virtual ~SseStreamHandler() = default;

  virtual void pushSseHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) = 0;
  virtual void pushSseData(Buffer::Instance& data, bool end_stream) = 0;

  /**
   * Push a single SSE event containing a JSON-RPC message to the client.
   * Used during aggregation to forward intermediate events (notifications, server requests)
   * while buffering responses for merging.
   * @param backend_name The backend that sent this event.
   * @param event_data The JSON-RPC message data.
   * @param event_type The classified type of the message.
   */
  virtual void pushSseEvent(const std::string& backend_name, const std::string& event_data,
                            SseMessageType event_type) = 0;

  virtual void onStreamingError(absl::string_view error) = 0;
  virtual void onStreamingComplete() = 0;
};

/**
 * Callbacks for handling async HTTP responses from backend MCP servers.
 * Accumulates response data and invokes completion callback when stream ends.
 *
 * SSE Processing (aggregate mode):
 * - Parses SSE events, forwarding notifications immediately while caching responses
 * - Completes early when Response is found (SSE streams may not have end_stream)
 *
 * SSE Pass-through: Buffers body and forwards directly for tools/call.
 *
 * Async Safety: Uses weak_ptr to safely handle filter destruction during async callbacks.
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
   * @param parent Weak pointer to the parent SSE stream handler for streaming updates.
   * @param streaming_enabled If true, enables SSE streaming pass-through mode.
   */
  BackendStreamCallbacks(const std::string& backend_name,
                         std::function<void(BackendResponse)> on_complete, int64_t request_id = 0,
                         bool aggregate_mode = false, std::weak_ptr<SseStreamHandler> parent = {},
                         bool streaming_enabled = false);

  // AsyncClient::StreamCallbacks
  void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
  void onComplete() override;
  void onReset() override;

private:
  void complete();

  /**
   * Incrementally parses SSE events, classifies them, and forwards intermediate events.
   * Caches the Response in extracted_jsonrpc when found.
   * @return true if a matching JSON-RPC response was found.
   */
  bool tryParseSseResponse();

  std::string backend_name_;
  std::function<void(BackendResponse)> on_complete_;
  int64_t request_id_{0};
  bool aggregate_mode_{false};
  std::weak_ptr<SseStreamHandler> parent_; // Safe handle to parent for streaming
  bool streaming_enabled_{false};          // Enable streaming pass-through
  bool streaming_started_{false};          // Track if streaming headers sent
  size_t parse_offset_{0};                 // Track SSE parse position for incremental parsing
  bool found_response_{false};             // Cache result to avoid re-parsing
  BackendResponse response_;
  bool completed_{false};
};

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
