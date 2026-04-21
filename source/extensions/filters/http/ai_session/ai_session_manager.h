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
 * AiSessionManager — per-worker session store and filter-chain factory.
 *
 * Responsibilities:
 *   1. Implement JsonRpcConnectionManagerCallbacks::newStream() to receive
 *      each JSON-RPC request from JsonRpcConnectionManager.
 *   2. Look up an existing AiSession by Mcp-Session-Id header, or create one.
 *   3. Build a fresh AiFilterChain per request by calling each AiFilterFactory.
 *   4. Return the chain as a JsonRpcDecoder& to drive parse-event dispatch.
 *
 * Session lifetime:
 *   Sessions are created on the first request carrying a session ID and
 *   removed via destroySession() (e.g. on DELETE /mcp or timeout).
 *
 * Per-worker scope:
 *   One AiSessionManager lives per Envoy worker thread (via TLS).  Session
 *   state is only coherent across requests that land on the same worker —
 *   i.e., the same TCP connection.  If the MCP client reconnects and the new
 *   connection is dispatched to a different worker, the session map on that
 *   worker will not contain the old session.  No cross-worker session sharing
 *   is implemented; connection-level affinity is assumed.
 *
 * Concurrency within one connection:
 *   For HTTP/1.1, one session has at most one in-flight request at a time.
 *   The active AiFilterChain is owned by the AiSession and replaced on each
 *   new request.  HTTP/2 multiplexing would require a list of active chains.
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
   * Extracts the session ID, looks up or creates the AiSession, builds a
   * fresh AiFilterChain, and returns it as a JsonRpcDecoder&.
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

  std::vector<std::unique_ptr<AiStreamFilter>> buildFilters();

  // -------------------------------------------------------------------------
  // State
  // -------------------------------------------------------------------------

  // Registered filter factories — called for every new JSON-RPC request to
  // produce the per-request chain.
  std::vector<AiFilterFactory> filter_factories_;

  // Live sessions, keyed by session ID.
  // Sessions outlive individual requests but are scoped to this worker thread.
  absl::flat_hash_map<std::string, std::unique_ptr<AiSession>> sessions_;

  // The default session used when the client sends no session ID.
  // Represents anonymous / sessionless callers.
  static constexpr absl::string_view kAnonymousSessionId = "__anonymous__";
};

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
