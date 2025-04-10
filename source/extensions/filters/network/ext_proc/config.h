#pragma once

#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

/**
 * Config registration for the network external filter. @see NamedNetworkFilterConfigFactory.
 */
class NetworkExtProcConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor> {
public:
  NetworkExtProcConfigFactory() : FactoryBase(NetworkFilterNames::get().NetworkExternalProcessor) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
