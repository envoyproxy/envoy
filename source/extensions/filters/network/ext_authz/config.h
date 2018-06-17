#pragma once

#include "envoy/config/filter/network/ext_authz/v2/ext_authz.pb.h"
#include "envoy/config/filter/network/ext_authz/v2/ext_authz.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

/**
 * Config registration for the  external authorization filter. @see NamedNetworkFilterConfigFactory.
 */
class ExtAuthzConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::ext_authz::v2::ExtAuthz> {
public:
  ExtAuthzConfigFactory() : FactoryBase(NetworkFilterNames::get().EXT_AUTHORIZATION) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::ext_authz::v2::ExtAuthz& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
