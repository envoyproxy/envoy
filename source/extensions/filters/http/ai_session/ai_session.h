#pragma once

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {
class AiFilterChain; // forward declaration to avoid circular include
} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

/**
 * Per-session state shared across multiple JSON-RPC exchanges.
 *
 * MCP defines an explicit session lifecycle: one initialize/initialized
 * handshake followed by N tool-call / resource-read exchanges sharing the
 * same session ID.  AiSession is the persistent object for that lifecycle;
 * it survives across many HTTP requests.  AiSessionManager owns all live
 * sessions in a map keyed by session ID.
 *
 * Each HTTP request creates a fresh AiFilterChain with a reference to the
 * AiSession so filters can read and write cross-request state (auth,
 * capabilities, context window) without repeating initialization work.
 */
class AiSession {
public:
  explicit AiSession(std::string session_id) : session_id_(std::move(session_id)) {}

  // -------------------------------------------------------------------------
  // Identity
  // -------------------------------------------------------------------------

  const std::string& sessionId() const { return session_id_; }

  // -------------------------------------------------------------------------
  // Initialisation handshake state
  //
  // MCP requires an initialize → initialized round-trip before any tool calls.
  // Other AI protocols have similar capability-negotiation phases.
  // -------------------------------------------------------------------------

  bool isInitialized() const { return initialized_; }

  void markInitialized(nlohmann::json client_capabilities,
                       absl::string_view negotiated_protocol_version) {
    initialized_ = true;
    client_capabilities_ = std::move(client_capabilities);
    negotiated_protocol_version_ = std::string(negotiated_protocol_version);
  }

  const nlohmann::json& clientCapabilities() const { return client_capabilities_; }
  const std::string& negotiatedProtocolVersion() const { return negotiated_protocol_version_; }

  // -------------------------------------------------------------------------
  // Auth / identity
  //
  // Set by an auth filter during the first (or any) request; read by
  // downstream filters and route decisions.
  // -------------------------------------------------------------------------

  void setIdentity(std::string principal) { principal_ = std::move(principal); }
  const std::string& principal() const { return principal_; }
  bool isAuthenticated() const { return !principal_.empty(); }

  // -------------------------------------------------------------------------
  // Context window / conversation state
  //
  // AI sessions accumulate a context across turns (tool call N references
  // the result of tool call N-1). Filters can append to this context and
  // read it to inject history into upstream requests.
  // -------------------------------------------------------------------------

  void appendContext(nlohmann::json turn) { context_.push_back(std::move(turn)); }

  const nlohmann::json& context() const { return context_; }

  void clearContext() { context_ = nlohmann::json::array(); }

  // -------------------------------------------------------------------------
  // -------------------------------------------------------------------------
  // Active request management
  // -------------------------------------------------------------------------

  /**
   * Attach a new AiFilterChain for an incoming request.  Replaces any
   * previously active chain (one active request per session for HTTP/1.1).
   * @return Reference to the chain; valid until the next beginRequest() call.
   */
  AiFilterChain& beginRequest(std::unique_ptr<AiFilterChain> chain) {
    active_request_ = std::move(chain);
    return *active_request_;
  }

  AiFilterChain* activeRequest() { return active_request_.get(); }

  // -------------------------------------------------------------------------
  // Request counter (monotonic within the session)
  // -------------------------------------------------------------------------

  uint64_t requestCount() const { return request_count_; }
  uint64_t nextRequestIndex() { return ++request_count_; }

private:
  std::string session_id_;

  // Handshake
  bool initialized_{false};
  nlohmann::json client_capabilities_;
  std::string negotiated_protocol_version_;

  // Auth
  std::string principal_;

  // Context window
  nlohmann::json context_{nlohmann::json::array()};

  // Monotonic request index within this session.
  uint64_t request_count_{0};

  // Currently active per-request filter chain.  Owned by the session.
  std::unique_ptr<AiFilterChain> active_request_;
};

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
