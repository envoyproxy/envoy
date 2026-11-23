#include "source/extensions/filters/network/rbac/config.h"

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/rbac/utility.h"
#include "source/extensions/filters/network/rbac/rbac_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "xds/type/matcher/v3/matcher.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

absl::StatusOr<Network::FilterFactoryCb>
RoleBasedAccessControlNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::rbac::v3::RBAC& proto_config,
    Server::Configuration::FactoryContext& context) {
  if (proto_config.has_rules()) {
    RETURN_IF_NOT_OK(Filters::Common::RBAC::validateRbacRules(proto_config.rules()));
  }
  if (proto_config.has_shadow_rules()) {
    RETURN_IF_NOT_OK(Filters::Common::RBAC::validateRbacRules(proto_config.shadow_rules()));
  }
  RoleBasedAccessControlFilterConfigSharedPtr config(
      std::make_shared<RoleBasedAccessControlFilterConfig>(proto_config, context.scope(),
                                                           context.serverFactoryContext(),
                                                           context.messageValidationVisitor()));
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<RoleBasedAccessControlFilter>(config));
  };
}

/**
 * Static registration for the RBAC network filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RoleBasedAccessControlNetworkFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
