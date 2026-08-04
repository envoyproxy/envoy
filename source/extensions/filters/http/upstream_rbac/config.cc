#include "source/extensions/filters/http/upstream_rbac/config.h"

#include "envoy/registry/registry.h"

#include "source/common/common/empty_string.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"
#include "source/extensions/filters/http/upstream_rbac/upstream_rbac_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace UpstreamRBACFilter {

absl::StatusOr<Http::FilterFactoryCb>
UpstreamRoleBasedAccessControlFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string&,
    Server::Configuration::UpstreamFactoryContext& context) {
  auto& server_context = context.serverFactoryContext();
  const auto& typed_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::filters::http::rbac::v3::RBAC&>(
          proto_config, server_context.messageValidationVisitor());

  // The stats prefix supplied for an upstream filter is the owning cluster's scope prefix (e.g.
  // "cluster.<name>."), which context.scope() already applies. Use an empty prefix so the RBAC
  // stats land under "cluster.<name>.rbac." instead of being prefixed twice.
  auto config = std::make_shared<RBACFilter::RoleBasedAccessControlFilterConfig>(
      typed_config, EMPTY_STRING, context.scope(), server_context,
      server_context.messageValidationVisitor());

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
