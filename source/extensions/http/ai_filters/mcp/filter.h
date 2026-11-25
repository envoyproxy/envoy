#pragma once

#include <functional>
#include <optional>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Mcp {

// Runs the MCP filter's inspection in the AI Protocol Manager: the manager holds and parses the
// request, and the inspector the MCP HTTP filter shares reads the parsed document.
class McpFilter : public Common::SyncAiFilter, public HttpFilters::Mcp::McpInspectorCallbacks {
public:
  McpFilter(HttpFilters::Mcp::McpFilterConfigSharedPtr config,
            const HttpFilters::AiProtocolManager::AiFilterContext& context);

  // Common::SyncAiFilter
  absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                          HttpFilters::AiProtocolManager::LocalReplier reply_locally) override;

  // HttpFilters::AiProtocolManager::AiFilter
  std::optional<HttpFilters::AiProtocolManager::LocalReply>
  onUnparsedRequest(HttpFilters::AiProtocolManager::UnparsedRequest reason) override;
  // A body the inspector does not reject goes upstream, as it does past the MCP HTTP filter.
  bool forwardsUnparsedRequest() const override { return true; }

  // HttpFilters::Mcp::McpInspectorCallbacks
  StreamInfo::StreamInfo& streamInfo() override { return context_.stream_info; }
  Http::RequestHeaderMapOptRef requestHeaders() override { return context_.request_headers; }
  const HttpFilters::Mcp::McpOverrideConfig* routeOverride() const override;
  void clearRouteCache() override { context_.clear_route_cache(); }
  void sendLocalReply(Http::Code code, absl::string_view body,
                      std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                      absl::string_view details) override;

private:
  const HttpFilters::AiProtocolManager::AiFilterContext context_;
  HttpFilters::Mcp::McpInspector inspector_;
  // The inspector's reply, held until the manager is handed it.
  std::optional<HttpFilters::AiProtocolManager::LocalReply> local_reply_;
};

} // namespace Mcp
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
