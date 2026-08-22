#include "source/extensions/filters/http/router/config.h"

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"

#include "source/common/router/router.h"
#include "source/common/router/shadow_writer_impl.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {

absl::StatusOr<Http::FilterFactoryCb> RouterFilterConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::router::v3::Router& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  // The stat prefix name must be created in the symbol table of the same scope that will be used
  // to create the stats.
  Stats::Scope& scope = extra_context.scopeOr(context);
  Stats::StatNameManagedStorage prefix(extra_context.stats_prefix, scope.symbolTable());
  Server::GenericFactoryContextImpl generic_context(context, scope, extra_context.visitor,
                                                    extra_context.init_manager);
  auto config_or_error = Router::FilterConfig::create(
      prefix.statName(), generic_context,
      std::make_unique<Router::ShadowWriterImpl>(context.clusterManager()), proto_config);
  RETURN_IF_NOT_OK_REF(config_or_error.status());
  Router::FilterConfigSharedPtr filter_config(std::move(*config_or_error));

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<Router::ProdFilter>(filter_config, filter_config->default_stats_));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(RouterFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.router");

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
