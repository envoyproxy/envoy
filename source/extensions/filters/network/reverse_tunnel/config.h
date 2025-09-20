#pragma once

#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

/**
 * Config registration for the reverse tunnel network filter.
 */
class ReverseTunnelFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel> {
public:
  // Always mark the reverse tunnel filter as terminal filter.
  ReverseTunnelFilterConfigFactory()
      : FactoryBase(NetworkFilterNames::get().ReverseTunnel, true /* isTerminalFilter */) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
