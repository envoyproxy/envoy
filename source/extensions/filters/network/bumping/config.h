#pragma once

#include "envoy/extensions/filters/network/bumping/v3/bumping.pb.h"
#include "envoy/extensions/filters/network/bumping/v3/bumping.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Bumping {

/**
 * Config registration for the bumping filter. @see NamedNetworkFilterConfigFactory.
 */

class ConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::bumping::v3::Bumping> {
public:
  ConfigFactory() : FactoryBase(NetworkFilterNames::get().Bumping, false) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::bumping::v3::Bumping& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace Bumping
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
