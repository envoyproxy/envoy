#pragma once

#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"
#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

/**
 * Config factory for MCP router filter.
 */
class McpRouterFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::mcp_router::v3::McpRouter> {
public:
  McpRouterFilterConfigFactory() : FactoryBase("envoy.filters.http.mcp_router") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
