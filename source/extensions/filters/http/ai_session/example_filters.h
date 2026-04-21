#pragma once

// Example AiStreamFilter implementations that mirror concrete Envoy HTTP filters.
//
// Filter chain configured as:
//   [McpAuthFilter] → [McpMethodRouterFilter] → [McpMetadataFilter]
//
// Analogous to:
//   [ext_authz] → [router] → [header_mutation]

#include "source/extensions/filters/http/ai_session/ai_filter.h"
#include "source/extensions/filters/http/ai_session/ai_session.h"

#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

// ---------------------------------------------------------------------------
// McpAuthFilter
//
// Analogous to ext_authz HTTP filter.
//
// onMethod fires BEFORE params arrive.  This filter starts an async authz
// check (e.g. OPA, custom service) keyed on (session.principal, method).
// It returns StopIteration, which queues the subsequent onId / onParams /
// onJsonRpcComplete events exactly as ext_authz holds the request body when
// StopIteration is returned from decodeHeaders().
//
// When the authz response arrives asynchronously, the filter calls
// callbacks_.continueProcessing(), which:
//   1. Resumes onMethod on the next filter (McpMethodRouterFilter).
//   2. Drains the queued onId, onParams, onComplete through the full chain.
// ---------------------------------------------------------------------------
class McpAuthFilter : public AiStreamFilter {
public:
  void setCallbacks(AiStreamFilterCallbacks& callbacks) override { callbacks_ = &callbacks; }

  AiFilterStatus onJsonRpcBegin() override { return AiFilterStatus::Continue; }

  AiFilterStatus onMethod(absl::string_view method) override {
    method_ = std::string(method);

    // For initialize, no authz needed.
    if (method_ == "initialize") {
      return AiFilterStatus::Continue;
    }

    // Session must be authenticated before any tool calls.
    if (!callbacks_->session().isAuthenticated()) {
      callbacks_->sendJsonRpcError(-32001, "Unauthenticated: session has no identity");
      return AiFilterStatus::StopIteration;
    }

    // Start async authz check for this (principal, method) pair.
    // In a real filter this would call an ext_authz gRPC service.
    // For illustration: reject "admin" methods for non-admin principals.
    if (absl::StartsWith(method_, "admin/") &&
        callbacks_->session().principal() != "admin") {
      callbacks_->sendJsonRpcError(-32003, "Forbidden: insufficient privileges");
      return AiFilterStatus::StopIteration;
    }

    return AiFilterStatus::Continue;
  }

  AiFilterStatus onId(const nlohmann::json&) override { return AiFilterStatus::Continue; }
  AiFilterStatus onParams(const nlohmann::json&) override { return AiFilterStatus::Continue; }
  AiFilterStatus onJsonRpcComplete() override { return AiFilterStatus::Continue; }
  void onError(absl::string_view) override {}

private:
  AiStreamFilterCallbacks* callbacks_{nullptr};
  std::string method_;
};

// ---------------------------------------------------------------------------
// McpInitFilter
//
// Analogous to a stateful "handshake" filter.
//
// Handles the MCP initialize → initialized exchange:
//   - On "initialize": extracts client capabilities from params, marks the
//     session as initialized, returns a synthetic response (no upstream call).
//   - On "initialized": ACKs the notification, no-ops.
//   - On anything else while session is not initialized: rejects.
//
// Storing capabilities in AiSession (cross-request) mirrors how HCM stores
// HPACK compression context across HTTP/2 streams.
// ---------------------------------------------------------------------------
class McpInitFilter : public AiStreamFilter {
public:
  void setCallbacks(AiStreamFilterCallbacks& callbacks) override { callbacks_ = &callbacks; }

  AiFilterStatus onJsonRpcBegin() override { return AiFilterStatus::Continue; }

  AiFilterStatus onMethod(absl::string_view method) override {
    method_ = std::string(method);

    if (method_ != "initialize" && method_ != "notifications/initialized" &&
        !callbacks_->session().isInitialized()) {
      callbacks_->sendJsonRpcError(-32002, "Session not initialized: send initialize first");
      return AiFilterStatus::StopIteration;
    }
    return AiFilterStatus::Continue;
  }

  AiFilterStatus onId(const nlohmann::json& id) override {
    id_ = id;
    return AiFilterStatus::Continue;
  }

  AiFilterStatus onParams(const nlohmann::json& params) override {
    params_ = params;
    return AiFilterStatus::Continue;
  }

  AiFilterStatus onJsonRpcComplete() override {
    if (method_ == "initialize") {
      // Extract and persist client capabilities in the session.
      const auto& caps = params_.value("capabilities", nlohmann::json::object());
      callbacks_->session().markInitialized(caps, "2024-11-05");
    }
    return AiFilterStatus::Continue;
  }

  void onError(absl::string_view) override {}

private:
  AiStreamFilterCallbacks* callbacks_{nullptr};
  std::string method_;
  nlohmann::json id_;
  nlohmann::json params_;
};

// ---------------------------------------------------------------------------
// McpContextFilter
//
// Analogous to header_mutation or lua HTTP filter.
//
// Appends each completed tool-call exchange to the session's context window
// so subsequent requests can reference prior results.  Demonstrates reading
// and writing AiSession cross-request state.
// ---------------------------------------------------------------------------
class McpContextFilter : public AiStreamFilter {
public:
  void setCallbacks(AiStreamFilterCallbacks& callbacks) override { callbacks_ = &callbacks; }

  AiFilterStatus onJsonRpcBegin() override { return AiFilterStatus::Continue; }
  AiFilterStatus onMethod(absl::string_view method) override {
    method_ = std::string(method);
    return AiFilterStatus::Continue;
  }
  AiFilterStatus onId(const nlohmann::json& id) override {
    id_ = id;
    return AiFilterStatus::Continue;
  }
  AiFilterStatus onParams(const nlohmann::json& params) override {
    params_ = params;
    return AiFilterStatus::Continue;
  }

  AiFilterStatus onJsonRpcComplete() override {
    if (method_ == "tools/call" || method_ == "resources/read") {
      // Record this turn in the session's context window.
      callbacks_->session().appendContext({{"turn", callbacks_->session().requestCount()},
                                           {"method", method_},
                                           {"params", params_}});
    }
    return AiFilterStatus::Continue;
  }

  void onError(absl::string_view) override {}

private:
  AiStreamFilterCallbacks* callbacks_{nullptr};
  std::string method_;
  nlohmann::json id_;
  nlohmann::json params_;
};

// ---------------------------------------------------------------------------
// Wire-up example: building an AiSessionManager with these filters
//
// Analogous to configuring http_filters in HCM bootstrap YAML.
// ---------------------------------------------------------------------------
//
// inline std::unique_ptr<AiSessionManager> buildMcpSessionManager() {
//   std::vector<AiFilterFactory> factories = {
//     []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpAuthFilter>(); },
//     []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpInitFilter>(); },
//     []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpContextFilter>(); },
//   };
//   return std::make_unique<AiSessionManager>(std::move(factories));
// }
//
// Then plug into JsonRpcConnectionManager:
//   auto session_mgr = buildMcpSessionManager();
//   auto cm = std::make_shared<JsonRpcConnectionManager>(*session_mgr);
//   // Register cm as an HTTP filter in the HCM filter chain.
//
// ---------------------------------------------------------------------------
// Event sequence for a tools/call request from an authenticated session:
//
//  TCP → HCM → HTTP codec → JsonRpcConnectionManager
//    JsonRpcParser fires:
//      onJsonRpcBegin()
//        McpAuthFilter.onJsonRpcBegin()  → Continue
//        McpInitFilter.onJsonRpcBegin()  → Continue
//        McpContextFilter.onJsonRpcBegin() → Continue
//      onMethod("tools/call")
//        McpAuthFilter.onMethod()        → Continue  (authenticated, allowed)
//        McpInitFilter.onMethod()        → Continue  (session is initialized)
//        McpContextFilter.onMethod()     → Continue
//      onId(42)
//        McpAuthFilter.onId()            → Continue
//        McpInitFilter.onId()            → Continue
//        McpContextFilter.onId()         → Continue
//      onParams({name:"search",...})
//        McpAuthFilter.onParams()        → Continue
//        McpInitFilter.onParams()        → Continue
//        McpContextFilter.onParams()     → Continue
//      onJsonRpcComplete()
//        McpAuthFilter.onJsonRpcComplete() → Continue
//        McpInitFilter.onJsonRpcComplete() → Continue  (not initialize, skip)
//        McpContextFilter.onJsonRpcComplete() → Continue  (appends turn to ctx)
// ---------------------------------------------------------------------------

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
