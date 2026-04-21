#pragma once

#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

class AiSession;
class AiStreamFilterCallbacks;

/**
 * Return value from AiStreamFilter event handlers.
 *
 * Mirrors Http::FilterHeadersStatus / FilterDataStatus:
 *   Continue      → advance to the next filter in the chain immediately.
 *   StopIteration → pause the chain; call continueProcessing() to resume.
 *                   Events that arrive while stopped are queued and replayed
 *                   in document order when the chain resumes, exactly as
 *                   FilterManager buffers body data when headers StopIteration.
 */
enum class AiFilterStatus {
  Continue,
  StopIteration,
};

/**
 * Per-request callbacks injected into each AiStreamFilter.
 *
 * Mirrors Http::StreamDecoderFilterCallbacks: a filter calls these to
 * interact with the chain and the surrounding infrastructure.
 */
class AiStreamFilterCallbacks {
public:
  virtual ~AiStreamFilterCallbacks() = default;

  /**
   * Resume the paused filter chain.
   *
   * Analogous to StreamDecoderFilterCallbacks::continueDecoding():
   *   - The current event (the one that returned StopIteration) continues
   *     from the NEXT filter in the chain.
   *   - Any events that arrived while the chain was paused are replayed
   *     in order, starting from filter 0 (same as how FilterManager
   *     replays buffered data after continueDecoding()).
   *
   * Must only be called after a handler returned StopIteration.
   */
  virtual void continueProcessing() = 0;

  /**
   * Terminate this request with a JSON-RPC error response.
   *
   * Analogous to StreamDecoderFilterCallbacks::sendLocalReply():
   * sends an error directly to the caller and stops filter chain iteration.
   *
   * @param code    JSON-RPC error code (e.g. -32603 internal error).
   * @param message Human-readable error message.
   * @param data    Optional additional error data (null if absent).
   */
  virtual void sendJsonRpcError(int code, absl::string_view message,
                                const nlohmann::json& data = nlohmann::json{}) = 0;

  /**
   * The session this request belongs to.
   *
   * Analogous to StreamFilterCallbacks::streamInfo(): provides access to
   * cross-request state (auth, capabilities, negotiated version, etc.).
   */
  virtual AiSession& session() = 0;
};

/**
 * A single filter in the AI processing chain.
 *
 * Mirrors Http::StreamDecoderFilter: one instance per filter per request.
 * The filter chain manager (AiFilterChain) calls these methods in declaration
 * order as JSON-RPC fields are parsed from the HTTP body.
 *
 * Ordering contract (mirrors headers → data → trailers):
 *
 *   setCallbacks  (once, before any event)
 *   onJsonRpcBegin
 *   onMethod | onId | onParams  (in JSON document order, any subset)
 *   onJsonRpcComplete  OR  onError
 *
 * A filter may return StopIteration on any event. While stopped, no further
 * events are delivered to any filter. Call continueProcessing() to resume.
 */
class AiStreamFilter {
public:
  virtual ~AiStreamFilter() = default;

  /**
   * Inject the per-request callbacks.  Called once before any event method.
   * Analogous to StreamDecoderFilter::setDecoderFilterCallbacks().
   */
  virtual void setCallbacks(AiStreamFilterCallbacks& callbacks) = 0;

  /**
   * The JSON-RPC object's opening '{' has been consumed.
   * No fields are available yet — use this for per-request initialisation.
   * Analogous to decodeHeaders() with end_stream=false.
   */
  virtual AiFilterStatus onJsonRpcBegin() = 0;

  /**
   * The "method" string field is fully parsed.
   *
   * Fires before onParams, potentially on the first body chunk.
   * Returning StopIteration here is the main hook for auth / routing
   * decisions that must complete before params are processed.
   */
  virtual AiFilterStatus onMethod(absl::string_view method) = 0;

  /**
   * The "id" field is fully parsed.  May fire before or after onMethod
   * depending on document order.
   */
  virtual AiFilterStatus onId(const nlohmann::json& id) = 0;

  /**
   * The entire "params" value is parsed and delivered.
   *
   * This fires only after all nested bytes of the params object/array
   * have been consumed — analogous to decodeData() with end_stream=true.
   */
  virtual AiFilterStatus onParams(const nlohmann::json& params) = 0;

  /**
   * The top-level '}' has been consumed: full message is available.
   * Analogous to decodeData(data, end_stream=true).
   * No further events will fire after this.
   */
  virtual AiFilterStatus onJsonRpcComplete() = 0;

  /**
   * A parse or protocol error occurred.  No further events will fire.
   * The filter should not call continueProcessing() after this.
   */
  virtual void onError(absl::string_view message) = 0;
};

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
