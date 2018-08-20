#pragma once

#include "envoy/config/filter/network/rbac/v2/rbac.pb.h"
#include "envoy/config/filter/network/rbac/v2/rbac.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

/**
 * Config registration for the RBAC network filter. @see NamedNetworkFilterConfigFactory.
 */
class RoleBasedAccessControlNetworkFilterConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::rbac::v2::RBAC> {

public:
  RoleBasedAccessControlNetworkFilterConfigFactory()
      : FactoryBase(NetworkFilterNames::get().Rbac) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::rbac::v2::RBAC& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
