#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_session/ai_filter.h"
#include "source/extensions/filters/http/ai_session/ai_filter_chain.h"
#include "source/extensions/filters/http/ai_session/ai_session.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_connection_manager.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_decoder.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

/**
 * Factory function type for creating one AiStreamFilter per request.
 *
 * Mirrors the Http::FilterFactory / FilterFactoryCb pattern: a list of these
 * is stored in AiSessionManager and called for every new JSON-RPC request to
 * build the per-request filter chain.
 */
using AiFilterFactory = std::function<std::unique_ptr<AiStreamFilter>()>;

/**
 * AiSessionManager — the ConnectionManagerImpl analog for the AI layer.
 *
 * Responsibilities:
 *
 *   1. Implement JsonRpcConnectionManagerCallbacks::newStream() so that
 *      JsonRpcConnectionManager can hand off each JSON-RPC request here.
 *
 *   2. Manage the session map: look up an existing AiSession by the
 *      Mcp-Session-Id header (or equivalent), or create a new one if absent.
 *      This is the cross-request lifecycle that HCM does not need to handle
 *      (HTTP is stateless per-request; MCP / AI protocols are not).
 *
 *   3. For each request: instantiate a fresh AiFilterChain by calling each
 *      registered AiFilterFactory once.  This is analogous to HCM calling
 *      each FilterFactory in its http_filters list for every new ActiveStream.
 *
 *   4. Return the AiFilterChain (as a JsonRpcDecoder&) to the caller; from
 *      that point the chain drives itself via the JsonRpcDecoder callbacks.
 *
 * Session lifetime:
 *   Sessions are created on the first request that carries a session ID.
 *   They are removed via destroySession() (e.g. when a DELETE /mcp is seen
 *   or the session times out).
 *
 * Concurrency:
 *   For HTTP/1.1 one session handles at most one in-flight request at a time.
 *   The active AiFilterChain is owned by the AiSession and replaced on every
 *   new request.  (HTTP/2 multiplexing would require a list of chains.)
 */
class AiSessionManager : public JsonRpc::JsonRpcConnectionManagerCallbacks,
                         public Logger::Loggable<Logger::Id::filter> {
public:
  // Header used to correlate requests to sessions.  MCP uses Mcp-Session-Id;
  // other protocols may substitute a different header.
  static constexpr absl::string_view kSessionIdHeader = "mcp-session-id";

  explicit AiSessionManager(std::vector<AiFilterFactory> filter_factories);

  // -------------------------------------------------------------------------
  // JsonRpcConnectionManagerCallbacks
  // -------------------------------------------------------------------------

  /**
   * Called once per JSON-RPC HTTP request by JsonRpcConnectionManager.
   *
   * Mirrors ServerConnectionCallbacks::newStream():
   *   - The codec calls newStream() once per HTTP request to get a
   *     RequestDecoder, then feeds it decodeHeaders / decodeData.
   *   - Here, JsonRpcConnectionManager calls newStream() once per request
   *     to get a JsonRpcDecoder, then feeds it onMethod / onId / onParams.
   *
   * What this method does:
   *   1. Extract session ID from `request_headers`.
   *   2. Look up or create the AiSession.
   *   3. Build a fresh AiFilterChain from filter_factories_.
   *   4. Inject `decoder_callbacks` into the chain so filters can call
   *      sendLocalReply() via sendJsonRpcError().
   *   5. Attach the chain to the session (AiSession::beginRequest).
   *   6. Return the chain as a JsonRpcDecoder&.
   */
  JsonRpc::JsonRpcDecoder&
  newStream(const Http::RequestHeaderMap& request_headers,
            Http::StreamDecoderFilterCallbacks& decoder_callbacks) override;

  // -------------------------------------------------------------------------
  // Session lifecycle
  // -------------------------------------------------------------------------

  /**
   * Explicitly destroy a session (e.g. on DELETE /mcp or timeout).
   * Returns true if the session existed.
   */
  bool destroySession(absl::string_view session_id);

  /** Number of currently live sessions. */
  size_t sessionCount() const { return sessions_.size(); }

  /** Access a session by ID (returns nullptr if not found). */
  AiSession* findSession(absl::string_view session_id);

private:
  // -------------------------------------------------------------------------
  // Helpers
  // -------------------------------------------------------------------------

  // Extract session ID from headers.  Returns empty string if absent.
  static std::string extractSessionId(const Http::RequestHeaderMap& headers);

  // Look up or create a session.
  AiSession& getOrCreateSession(const std::string& session_id);

  // Build a fresh per-request filter list from filter_factories_.
  // Mirrors HCM instantiating a FilterChain for each new ActiveStream.
  std::vector<std::unique_ptr<AiStreamFilter>> buildFilters();

  // -------------------------------------------------------------------------
  // State
  // -------------------------------------------------------------------------

  // Registered filter factories — analogous to HCM's list of FilterFactoryCb.
  // Called for every new JSON-RPC request to produce the per-request chain.
  std::vector<AiFilterFactory> filter_factories_;

  // Live sessions, keyed by session ID.
  // Analogous to HCM's streams_ list, but sessions outlive individual requests.
  absl::flat_hash_map<std::string, std::unique_ptr<AiSession>> sessions_;

  // The default session used when the client sends no session ID.
  // Represents anonymous / sessionless callers.
  static constexpr absl::string_view kAnonymousSessionId = "__anonymous__";
};

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
