#pragma once
#include "source/extensions/filters/http/common/factory_base.h"
#include "envoy/extensions/filters/http/mcp_proxy/v3/mcp_proxy.pb.h"
#include "envoy/extensions/filters/http/mcp_proxy/v3/mcp_proxy.pb.validate.h"
namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpProxy {
class McpProxyFilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::mcp_proxy::v3::McpProxy> {
public:
  McpProxyFilterConfig() : FactoryBase("envoy.filters.http.mcp_proxy") {}
private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mcp_proxy::v3::McpProxy&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::mcp_proxy::v3::McpProxy&,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};
} // namespace McpProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
