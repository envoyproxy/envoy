#include "source/extensions/filters/http/router/config.h"

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"

#include "source/common/router/router.h"
#include "source/common/router/shadow_writer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {

absl::StatusOr<Http::FilterFactoryCb> RouterFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::router::v3::Router& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  Stats::StatNameManagedStorage prefix(stat_prefix, context.scope().symbolTable());
  auto config_or_error = Router::FilterConfig::create(
      prefix.statName(), context,
      std::make_unique<Router::ShadowWriterImpl>(context.serverFactoryContext().clusterManager()),
      proto_config);
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
