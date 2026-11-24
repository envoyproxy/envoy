#include "source/extensions/http/ai_filters/mcp/filter.h"

#include <string>
#include <utility>

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Mcp {

using HttpFilters::AiProtocolManager::LocalReply;
using HttpFilters::AiProtocolManager::UnparsedRequest;
using HttpFilters::Mcp::McpInspector;

McpFilter::McpFilter(HttpFilters::Mcp::McpFilterConfigSharedPtr config,
                     const HttpFilters::AiProtocolManager::AiFilterContext& context)
    : context_(context), inspector_(std::move(config), *this) {}

absl::Status McpFilter::decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                                   HttpFilters::AiProtocolManager::LocalReplier reply_locally) {
  McpInspector::Result result =
      inspector_.onRequestHeaders(context_.request_headers, /*end_stream=*/false);
  if (result == McpInspector::Result::NeedBody) {
    result = inspector_.onRequestBody(request.json(), context_.request_payload_bytes);
  }
  if (result == McpInspector::Result::Replied) {
    std::move(reply_locally)(std::move(*local_reply_));
  }
  return absl::OkStatus();
}

std::optional<LocalReply> McpFilter::onUnparsedRequest(UnparsedRequest reason) {
  McpInspector::Result result = inspector_.onRequestHeaders(
      context_.request_headers, /*end_stream=*/reason == UnparsedRequest::NoBody);
  if (result == McpInspector::Result::NeedBody) {
    result = inspector_.onInvalidRequestBody();
  }
  return result == McpInspector::Result::Replied ? std::move(local_reply_) : std::nullopt;
}

const HttpFilters::Mcp::McpOverrideConfig* McpFilter::routeOverride() const {
  return dynamic_cast<const HttpFilters::Mcp::McpOverrideConfig*>(context_.route_config.get());
}

void McpFilter::sendLocalReply(Http::Code code, absl::string_view body,
                               std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                               absl::string_view details) {
  local_reply_ = LocalReply{.code = code,
                            .body = std::string(body),
                            .details = std::string(details),
                            .modify_headers = std::move(modify_headers)};
}

} // namespace Mcp
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
