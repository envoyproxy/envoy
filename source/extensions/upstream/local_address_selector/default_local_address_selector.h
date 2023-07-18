#pragma once

#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/socket.h"
#include "envoy/upstream/upstream.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {

/**
 * Default implementation of UpstreamLocalAddressSelector.
 */
class DefaultUpstreamLocalAddressSelector : public UpstreamLocalAddressSelector {
public:
  DefaultUpstreamLocalAddressSelector(
      ::Envoy::OptRef<const envoy::config::core::v3::BindConfig> bind_config,
      Network::ConnectionSocket::OptionsSharedPtr base_socket_options,
      Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options,
      absl::optional<std::string> cluster_name);

  // UpstreamLocalAddressSelector
  UpstreamLocalAddress getUpstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& endpoint_address,
      const Network::ConnectionSocket::OptionsSharedPtr& socket_options) const override;

private:
  std::vector<UpstreamLocalAddress> upstream_local_addresses_;
};

/**
 * Utility functions to create UpstreamLocalAddressSelector.
 */
Network::ConnectionSocket::OptionsSharedPtr combineConnectionSocketOptions(
    const Network::ConnectionSocket::OptionsSharedPtr& local_address_options,
    const Network::ConnectionSocket::OptionsSharedPtr& options);

} // namespace Upstream
} // namespace Envoy
