#include "source/extensions/http/ai_filters/mcp/config.h"

#include <limits>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"
#include "source/extensions/http/ai_filters/mcp/filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Mcp {

absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
McpFilterConfigFactory::createAiFilterFactory(const Protobuf::Message& config,
                                              Server::Configuration::ServerFactoryContext& context,
                                              Stats::Scope& scope) {
  const auto& proto =
      MessageUtil::downcastAndValidate<const envoy::extensions::filters::http::mcp::v3::Mcp&>(
          config, context.messageValidationVisitor());
  auto filter_config =
      std::make_shared<HttpFilters::Mcp::McpFilterConfig>(proto, "ai_protocol_manager.", scope);
  return [filter_config](const HttpFilters::AiProtocolManager::AiFilterContext& stream_context)
             -> HttpFilters::AiProtocolManager::AiFilterSharedPtr {
    return std::make_shared<McpFilter>(filter_config, stream_context);
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
McpFilterConfigFactory::createRouteConfig(const Protobuf::Message& config,
                                          Server::Configuration::ServerFactoryContext&,
                                          ProtobufMessage::ValidationVisitor& validator) {
  const auto& proto = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::http::mcp::v3::McpOverride&>(config, validator);
  return std::make_shared<const HttpFilters::Mcp::McpOverrideConfig>(proto);
}

std::optional<uint32_t>
McpFilterConfigFactory::requestInlineStringBytes(const Protobuf::Message& config) {
  const uint32_t max_request_body_size = HttpFilters::Mcp::McpFilterConfig::maxRequestBodySize(
      dynamic_cast<const envoy::extensions::filters::http::mcp::v3::Mcp&>(config));
  return max_request_body_size > 0 ? max_request_body_size : std::numeric_limits<uint32_t>::max();
}

REGISTER_FACTORY(McpFilterConfigFactory, HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace Mcp
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
