#pragma once

#include "envoy/extensions/filters/network/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/network/fault/v3/fault.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Fault {

/**
 * Config registration for the network fault injection filter. @see NamedNetworkFilterConfigFactory.
 */
class FaultFilterFactory : public Common::ExceptionFreeFactoryBase<
                               envoy::extensions::filters::network::fault::v3::NetworkFault> {
public:
  FaultFilterFactory() : ExceptionFreeFactoryBase(NetworkFilterNames::get().Fault) {}

private:
  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::fault::v3::NetworkFault& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace Fault
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
