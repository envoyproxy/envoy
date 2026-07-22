#pragma once

#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

/**
 * Config registration for the external authorization filter. @see NamedNetworkFilterConfigFactory.
 */
class ExtAuthzConfigFactory : public Common::ExceptionFreeFactoryBase<
                                  envoy::extensions::filters::network::ext_authz::v3::ExtAuthz> {
public:
  ExtAuthzConfigFactory() : ExceptionFreeFactoryBase(NetworkFilterNames::get().ExtAuthorization) {}

private:
  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::ext_authz::v3::ExtAuthz& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
