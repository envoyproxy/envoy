#include "source/extensions/filters/http/mcp_router/config.h"

#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/config/datasource.h"
#include "source/extensions/filters/http/mcp_router/mcp_router.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

absl::StatusOr<Http::FilterFactoryCb> McpRouterFilterConfigFactory::createFilterFactory(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {

  std::string session_signing_key;
  if (proto_config.has_session_signing_key()) {
    // An explicitly configured but unreadable or empty key is a config error: silently ignoring
    // it would leave sessions unsigned while the operator believes tampering is rejected.
    auto key_or_error =
        Config::DataSource::read(proto_config.session_signing_key(), false, context.api());
    RETURN_IF_NOT_OK_REF(key_or_error.status());
    session_signing_key = std::move(key_or_error.value());
  }

  auto config = std::make_shared<McpRouterConfigImpl>(proto_config, stats_prefix, scope, context,
                                                      std::move(session_signing_key));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<McpRouterFilter>(config));
  };
}

absl::StatusOr<Http::FilterFactoryCb>
McpRouterFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::mcp_router::v3::McpRouter& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  return createFilterFactory(proto_config, extra_context.stats_prefix, context,
                             extra_context.scopeOr(context));
}

/**
 * Static registration for the MCP router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(McpRouterFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
