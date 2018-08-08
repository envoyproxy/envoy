#include "extensions/filters/network/rbac/config.h"

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/network/rbac/rbac_filter.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

Network::FilterFactoryCb
RoleBasedAccessControlNetworkFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::rbac::v2::RBAC& proto_config,
    Server::Configuration::FactoryContext& context) {
  Filters::Common::RBAC::RoleBasedAccessControlFilterConfigSharedPtr config(
      std::make_shared<Filters::Common::RBAC::RoleBasedAccessControlFilterConfig>(proto_config,
                                                                                  context.scope()));
  return [config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<RoleBasedAccessControlFilter>(config));
  };
}

/**
 * Static registration for the RBAC network filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RoleBasedAccessControlNetworkFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
