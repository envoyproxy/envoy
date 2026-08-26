#include "source/extensions/filters/http/upstream_rbac/config.h"

#include "envoy/registry/registry.h"

#include "source/common/common/empty_string.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"
#include "source/extensions/filters/http/upstream_rbac/upstream_rbac_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace UpstreamRBACFilter {

absl::StatusOr<Http::FilterFactoryCb>
UpstreamRoleBasedAccessControlFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::rbac::v3::RBAC& typed_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  // stats_prefix carries the parent's namespace ("http.<stat_prefix>." from a router,
  // "cluster.<name>." from a cluster) so RBAC counters land in the right place. This is a behavior
  // change, so it is guarded by the runtime flag; when disabled we fall back to the previous empty
  // prefix.
  const std::string& rbac_stats_prefix =
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.upstream_http_filters_correct_stats_prefix")
          ? extra_context.stats_prefix
          : EMPTY_STRING;
  auto config = std::make_shared<RBACFilter::RoleBasedAccessControlFilterConfig>(
      typed_config, rbac_stats_prefix, extra_context.scopeOr(context), context,
      extra_context.visitor);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<UpstreamRoleBasedAccessControlFilter>(config));
  };
}

/**
 * Static registration for the upstream RBAC filter. This filter is intentionally registered only as
 * an upstream HTTP filter; it acts from the onHostSelected() callback and has no effect in a
 * downstream filter chain. @see RegisterFactory.
 */
REGISTER_FACTORY(UpstreamRoleBasedAccessControlFilterConfigFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace UpstreamRBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
