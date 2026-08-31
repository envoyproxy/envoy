#pragma once

#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.validate.h"

#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
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
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel> {
public:
  // Always mark the reverse tunnel filter as terminal filter.
  ReverseTunnelFilterConfigFactory()
      : ExceptionFreeFactoryBase(NetworkFilterNames::get().ReverseTunnel,
                                 true /* isTerminalFilter */) {}

private:
  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
      Server::Configuration::FactoryContext& context) override;

  absl::Status validateConnLimit(
      const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config)
      const;
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
