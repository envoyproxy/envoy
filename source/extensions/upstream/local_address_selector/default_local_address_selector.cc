#include "source/extensions/upstream/local_address_selector/default_local_address_selector.h"

#include <string>

#include "envoy/network/socket.h"

#include "source/common/network/resolver_impl.h"
#include "source/common/network/socket_option_factory.h"

namespace Envoy {
namespace Upstream {

DefaultUpstreamLocalAddressSelector::DefaultUpstreamLocalAddressSelector(
    std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses,
    absl::optional<std::string> cluster_name)
    : upstream_local_addresses_(std::move(upstream_local_addresses)) {

  if (upstream_local_addresses_.size() == 0) {
    throw EnvoyException(fmt::format("{}'s upstream binding config has no valid source address.",
                                     !(cluster_name.has_value())
                                         ? "Bootstrap"
                                         : fmt::format("Cluster {}", cluster_name.value())));
  }

  if (upstream_local_addresses_.size() > 2) {
    throw EnvoyException(fmt::format(
        "{}'s upstream binding config has more than one extra/additional source addresses. Only "
        "one extra/additional source can be supported in BindConfig's "
        "extra_source_addresses/additional_source_addresses field",
        !(cluster_name.has_value()) ? "Bootstrap"
                                    : fmt::format("Cluster {}", cluster_name.value())));
  }

  // If we have more than one upstream address, they need to have different IP versions.
  if (upstream_local_addresses_.size() == 2) {
    // First verify that all address have valid IP address information.
    if (upstream_local_addresses_[0].address_ == nullptr ||
        upstream_local_addresses_[1].address_ == nullptr ||
        upstream_local_addresses_[0].address_->ip() == nullptr ||
        upstream_local_addresses_[1].address_->ip() == nullptr) {
      throw EnvoyException(fmt::format("{}'s upstream binding config has invalid IP addresses.",
                                       !(cluster_name.has_value())
                                           ? "Bootstrap"
                                           : fmt::format("Cluster {}", cluster_name.value())));
    }

    if (upstream_local_addresses_[0].address_->ip()->version() ==
        upstream_local_addresses_[1].address_->ip()->version()) {
      throw EnvoyException(fmt::format(
          "{}'s upstream binding config has two same IP version source addresses. Only two "
          "different IP version source addresses can be supported in BindConfig's source_address "
          "and extra_source_addresses/additional_source_addresses fields",
          !(cluster_name.has_value()) ? "Bootstrap"
                                      : fmt::format("Cluster {}", cluster_name.value())));
    }
  }
}

UpstreamLocalAddress DefaultUpstreamLocalAddressSelector::getUpstreamLocalAddress(
    const Network::Address::InstanceConstSharedPtr& endpoint_address,
    const Network::ConnectionSocket::OptionsSharedPtr& socket_options) const {
  for (auto& local_address : upstream_local_addresses_) {
    if (local_address.address_ == nullptr) {
      continue;
    }

    ASSERT(local_address.address_->ip() != nullptr);
    if (endpoint_address->ip() != nullptr &&
        local_address.address_->ip()->version() == endpoint_address->ip()->version()) {
      return {local_address.address_,
              combineConnectionSocketOptions(local_address.socket_options_, socket_options)};
    }
  }

  return {
      upstream_local_addresses_[0].address_,
      combineConnectionSocketOptions(upstream_local_addresses_[0].socket_options_, socket_options)};
}

Network::ConnectionSocket::OptionsSharedPtr combineConnectionSocketOptions(
    const Network::ConnectionSocket::OptionsSharedPtr& local_address_options,
    const Network::ConnectionSocket::OptionsSharedPtr& options) {
  Network::ConnectionSocket::OptionsSharedPtr connection_options =
      std::make_shared<Network::ConnectionSocket::Options>();

  if (options) {
    *connection_options = *options;
    Network::Socket::appendOptions(connection_options, local_address_options);
  } else {
    *connection_options = *local_address_options;
  }

  return connection_options;
}

} // namespace Upstream
} // namespace Envoy
