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
    : public Common::FactoryBase<envoy::extensions::filters::http::mcp::v3::Mcp> {
public:
  McpFilterConfigFactory() : FactoryBase("envoy.filters.http.mcp") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mcp::v3::Mcp& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
