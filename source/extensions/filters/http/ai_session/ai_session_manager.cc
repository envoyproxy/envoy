#include "source/extensions/filters/http/ai_session/ai_session_manager.h"

#include <memory>

#include "envoy/http/header_map.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

// =============================================================================
// Construction
// =============================================================================

AiSessionManager::AiSessionManager(std::vector<AiFilterFactory> filter_factories)
    : filter_factories_(std::move(filter_factories)) {}

// =============================================================================
// JsonRpcConnectionManagerCallbacks::newStream
//
// This is the seam between JsonRpcConnectionManager (the HTTP/JSON-RPC
// protocol layer) and the AI application layer.
//
// The full call chain on a new MCP request looks like this:
//
//   TCP bytes arrive
//     → HttpConnectionManager::onData()             [network filter]
//       → Http1::ConnectionImpl::dispatch()          [HTTP codec]
//         → ActiveStream::decodeHeaders()            [filter chain]
//           → JsonRpcConnectionManager::decodeHeaders()   [HTTP filter]
//             → AiSessionManager::newStream()          ← HERE
//               → getOrCreateSession()                [session lookup/create]
//               → buildFilters()                      [instantiate filter chain]
//               → session.beginRequest(chain)         [attach chain to session]
//               ← returns AiFilterChain& (as JsonRpcDecoder&)
//           → JsonRpcConnectionManager::decodeData()  [HTTP filter, per chunk]
//             → JsonRpcParser::parse()                [JSON-RPC state machine]
//               → AiFilterChain::onMethod()           [fires early, per field]
//               → AiFilterChain::onId()
//               → AiFilterChain::onParams()
//               → AiFilterChain::onJsonRpcComplete()
// =============================================================================

JsonRpc::JsonRpcDecoder&
AiSessionManager::newStream(const Http::RequestHeaderMap& request_headers,
                             Http::StreamDecoderFilterCallbacks& decoder_callbacks) {
  const std::string session_id = extractSessionId(request_headers);

  ENVOY_LOG(debug, "ai_session: newStream session_id='{}'",
            session_id.empty() ? kAnonymousSessionId : absl::string_view(session_id));

  AiSession& session =
      getOrCreateSession(session_id.empty() ? std::string(kAnonymousSessionId) : session_id);

  // Increment the request counter on the session before building the chain.
  session.nextRequestIndex();

  // Build a fresh AiFilterChain for this request.  The chain holds a
  // reference to the session so filters can read/write cross-request state.
  auto chain = std::make_unique<AiFilterChain>(session, buildFilters());

  // Inject HTTP filter callbacks so the chain can call sendLocalReply() when
  // a filter calls sendJsonRpcError().
  chain->setDecoderCallbacks(decoder_callbacks);

  // Store the chain on the session so it stays alive for the duration of
  // the HTTP request.  The JsonRpcConnectionManager holds a JsonRpcDecoder&
  // into this object.
  AiFilterChain& chain_ref = session.beginRequest(std::move(chain));
  return chain_ref;
}

// =============================================================================
// Session lifecycle
// =============================================================================

bool AiSessionManager::destroySession(absl::string_view session_id) {
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return false;
  }
  ENVOY_LOG(debug, "ai_session: destroying session '{}'", session_id);
  sessions_.erase(it);
  return true;
}

AiSession* AiSessionManager::findSession(absl::string_view session_id) {
  auto it = sessions_.find(session_id);
  return (it != sessions_.end()) ? it->second.get() : nullptr;
}

// =============================================================================
// Helpers
// =============================================================================

std::string AiSessionManager::extractSessionId(const Http::RequestHeaderMap& headers) {
  const Http::LowerCaseString session_header{std::string(kSessionIdHeader)};
  const auto& values = headers.get(session_header);
  if (values.empty()) {
    return {};
  }
  return std::string(values[0]->value().getStringView());
}

AiSession& AiSessionManager::getOrCreateSession(const std::string& session_id) {
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    return *it->second;
  }
  ENVOY_LOG(debug, "ai_session: creating new session '{}'", session_id);
  auto [inserted_it, ok] =
      sessions_.emplace(session_id, std::make_unique<AiSession>(session_id));
  (void)ok;
  return *inserted_it->second;
}

std::vector<std::unique_ptr<AiStreamFilter>> AiSessionManager::buildFilters() {
  // Each factory is called exactly once per request — mirrors HCM calling
  // each FilterFactoryCb to build an ActiveStream's decoder filter list.
  std::vector<std::unique_ptr<AiStreamFilter>> filters;
  filters.reserve(filter_factories_.size());
  for (const auto& factory : filter_factories_) {
    filters.push_back(factory());
  }
  return filters;
}

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
