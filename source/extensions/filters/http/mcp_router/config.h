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
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::mcp_router::v3::McpRouter> {
public:
  McpRouterFilterConfigFactory() : ExceptionFreeFactoryBase("envoy.filters.http.mcp_router") {}

private:
  bool
  isTerminalFilterByProtoTyped(const envoy::extensions::filters::http::mcp_router::v3::McpRouter&,
                               Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  // Shared factory creation used by both the downstream (FactoryContext) and route/vhost-level
  // (ServerFactoryContext) paths. Stats are scoped to the given scope.
  static absl::StatusOr<Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
      const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
      Stats::Scope& scope);
};

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
