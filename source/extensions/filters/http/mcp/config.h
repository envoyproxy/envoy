#pragma once

#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"
#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

/**
 * Config factory for MCP filter.
 */
class McpFilterConfigFactory
    : public Common::UnifiedFactoryBase<envoy::extensions::filters::http::mcp::v3::Mcp,
                                        envoy::extensions::filters::http::mcp::v3::McpOverride> {
public:
  McpFilterConfigFactory() : UnifiedFactoryBase("envoy.filters.http.mcp") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mcp::v3::Mcp& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::mcp::v3::McpOverride& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
