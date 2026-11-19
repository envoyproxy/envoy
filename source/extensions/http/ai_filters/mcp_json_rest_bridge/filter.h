#pragma once

#include <functional>
#include <optional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace McpJsonRestBridge {

namespace Bridge = HttpFilters::McpJsonRestBridge;

// MCP JSON REST Bridge AI filter: hosts the bridge on the AI Protocol Manager's filter chain.
// The bridge itself is the one the HTTP filter runs, so the two behave alike.
class McpJsonRestBridgeFilter : public Common::SyncAiFilter, public Bridge::BridgeCallbacks {
public:
  McpJsonRestBridgeFilter(Bridge::McpJsonRestBridgeFilterConfigSharedPtr config,
                          const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Common::SyncAiFilter
  absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                          HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

  // HttpFilters::AiProtocolManager::AiFilter
  std::optional<HttpFilters::AiProtocolManager::LocalReply>
  onUnparsedRequest(HttpFilters::AiProtocolManager::UnparsedRequest reason) override;
  HttpFilters::AiProtocolManager::ResponseMode onResponseHeaders(Http::ResponseHeaderMap& headers,
                                                                 bool end_stream) override;
  Coroutine::Task<absl::Status>
  encodeRaw(HttpFilters::AiProtocolManager::RawResponseReceiver receive,
            HttpFilters::AiProtocolManager::RawResponsePropagator propagate,
            HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

  // Bridge::BridgeCallbacks
  void sendLocalReply(Http::Code code, absl::string_view body,
                      std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                      Grpc::Status::GrpcStatus grpc_status, absl::string_view details,
                      Bridge::LocalReplyFor reply_for) override;
  void clearRouteCache() override { context_.clear_route_cache(); }
  StreamInfo::StreamInfo& streamInfo() override { return context_.stream_info; }
  absl::string_view filterConfigName() const override { return context_.filter_name; }
  OptRef<const Network::Connection> connection() override { return {}; }
  uint64_t streamId() const override;

private:
  // Runs the bridge over the request. Returns whether the request goes on; if not, the reply
  // is in local_reply_.
  bool bridgeRequest(HttpFilters::AiProtocolManager::AiRequest& request);

  // Takes the reply the bridge sent during the current call.
  HttpFilters::AiProtocolManager::LocalReply takeLocalReply();

  const Bridge::McpJsonRestBridgePerRouteConfig* routeConfig() const;

  Coroutine::Task<absl::Status>
  encodeStreaming(HttpFilters::AiProtocolManager::RawResponseReceiver receive,
                  HttpFilters::AiProtocolManager::RawResponsePropagator propagate,
                  HttpFilters::AiProtocolManager::LocalReplier reply_locally);
  Coroutine::Task<absl::Status>
  encodeBuffered(HttpFilters::AiProtocolManager::RawResponseReceiver receive,
                 HttpFilters::AiProtocolManager::RawResponsePropagator propagate,
                 HttpFilters::AiProtocolManager::LocalReplier reply_locally);

  const HttpFilters::AiProtocolManager::AiFilterContext context_;
  Bridge::McpJsonRestBridge bridge_;
  // The reply the bridge sent during the current call, for the caller to hand to the manager.
  std::optional<HttpFilters::AiProtocolManager::LocalReply> local_reply_;
  Bridge::McpJsonRestBridge::ResponseStatus response_status_{
      Bridge::McpJsonRestBridge::ResponseStatus::PassThrough};
  // The held headers of a Buffered response.
  Http::ResponseHeaderMap* response_headers_{nullptr};
  // The body of a Complete response.
  std::string response_body_;
};

} // namespace McpJsonRestBridge
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
