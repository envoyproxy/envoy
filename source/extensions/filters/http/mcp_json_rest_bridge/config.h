#pragma once

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.validate.h" // IWYU pragma: keep

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

/**
 * Config factory for MCP JSON REST bridge filter.
 */
class McpJsonRestBridgeFilterConfigFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge> {
public:
  McpJsonRestBridgeFilterConfigFactory() : ExceptionFreeFactoryBase(FilterName) {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
          proto_config,
      const std::string&, Server::Configuration::FactoryContext&) override;
};

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
