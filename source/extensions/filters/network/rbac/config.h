#pragma once

#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

/**
 * Config registration for the RBAC network filter. @see NamedNetworkFilterConfigFactory.
 */
class RoleBasedAccessControlNetworkFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::rbac::v3::RBAC> {

public:
  RoleBasedAccessControlNetworkFilterConfigFactory()
      : FactoryBase(NetworkFilterNames::get().Rbac) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::rbac::v3::RBAC& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
