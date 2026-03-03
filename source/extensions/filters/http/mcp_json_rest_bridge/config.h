#pragma once

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.validate.h" // IWYU pragma: keep

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

/**
 * Config factory for MCP JSON REST bridge filter.
 */
class McpJsonRestBridgeFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge> {
public:
  McpJsonRestBridgeFilterConfigFactory() : FactoryBase("envoy.filters.http.mcp_json_rest_bridge") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
          proto_config,
      const std::string&, Server::Configuration::FactoryContext&) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
          proto_config,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
