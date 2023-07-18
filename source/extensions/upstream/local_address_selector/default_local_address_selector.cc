#include "source/extensions/upstream/local_address_selector/default_local_address_selector.h"

#include <string>

#include "envoy/network/socket.h"

#include "source/common/network/resolver_impl.h"
#include "source/common/network/socket_option_factory.h"

namespace Envoy {
namespace Upstream {

DefaultUpstreamLocalAddressSelector::DefaultUpstreamLocalAddressSelector(
    ::Envoy::OptRef<const envoy::config::core::v3::BindConfig> bind_config,
    Network::ConnectionSocket::OptionsSharedPtr base_socket_options,
    Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options,
    absl::optional<std::string> cluster_name) {

  if (bind_config.has_value()) {
    if (bind_config->additional_source_addresses_size() > 0 &&
        bind_config->extra_source_addresses_size() > 0) {
      throw EnvoyException(fmt::format(
          "Can't specify both `extra_source_addresses` and `additional_source_addresses` "
          "in the {}'s upstream binding config",
          !(cluster_name.has_value()) ? "Bootstrap"
                                      : fmt::format("Cluster {}", cluster_name.value())));
    }

    if (bind_config->extra_source_addresses_size() > 1) {
      throw EnvoyException(fmt::format(
          "{}'s upstream binding config has more than one extra source addresses. Only one "
          "extra source can be supported in BindConfig's extra_source_addresses field",
          !(cluster_name.has_value()) ? "Bootstrap"
                                      : fmt::format("Cluster {}", cluster_name.value())));
    }

    if (bind_config->additional_source_addresses_size() > 1) {
      throw EnvoyException(fmt::format(
          "{}'s upstream binding config has more than one additional source addresses. Only one "
          "additional source can be supported in BindConfig's additional_source_addresses field",
          !(cluster_name.has_value()) ? "Bootstrap"
                                      : fmt::format("Cluster {}", cluster_name.value())));
    }

    if (!bind_config->has_source_address() &&
        (bind_config->extra_source_addresses_size() > 0 ||
         bind_config->additional_source_addresses_size() > 0)) {
      throw EnvoyException(fmt::format(
          "{}'s upstream binding config has extra/additional source addresses but no "
          "source_address. Extra/additional addresses cannot be specified if "
          "source_address is not set.",
          !(cluster_name.has_value()) ? "Bootstrap"
                                      : fmt::format("Cluster {}", cluster_name.value())));
    }

    UpstreamLocalAddress upstream_local_address;
    upstream_local_address.address_ =
        bind_config->has_source_address()
            ? Network::Address::resolveProtoSocketAddress(bind_config->source_address())
            : nullptr;
    upstream_local_address.socket_options_ = std::make_shared<Network::ConnectionSocket::Options>();

    Network::Socket::appendOptions(upstream_local_address.socket_options_, base_socket_options);
    Network::Socket::appendOptions(upstream_local_address.socket_options_, cluster_socket_options);

    upstream_local_addresses_.push_back(upstream_local_address);

    if (bind_config->extra_source_addresses_size() == 1) {
      UpstreamLocalAddress extra_upstream_local_address;
      extra_upstream_local_address.address_ = Network::Address::resolveProtoSocketAddress(
          bind_config->extra_source_addresses(0).address());
      ASSERT(extra_upstream_local_address.address_->ip() != nullptr &&
             upstream_local_address.address_->ip() != nullptr);
      if (extra_upstream_local_address.address_->ip()->version() ==
          upstream_local_address.address_->ip()->version()) {
        throw EnvoyException(fmt::format(
            "{}'s upstream binding config has two same IP version source addresses. Only two "
            "different IP version source addresses can be supported in BindConfig's source_address "
            "and extra_source_addresses fields",
            !(cluster_name.has_value()) ? "Bootstrap"
                                        : fmt::format("Cluster {}", cluster_name.value())));
      }

      extra_upstream_local_address.socket_options_ =
          std::make_shared<Network::ConnectionSocket::Options>();
      Network::Socket::appendOptions(extra_upstream_local_address.socket_options_,
                                     base_socket_options);

      if (bind_config->extra_source_addresses(0).has_socket_options()) {
        Network::Socket::appendOptions(
            extra_upstream_local_address.socket_options_,
            Network::SocketOptionFactory::buildLiteralOptions(
                bind_config->extra_source_addresses(0).socket_options().socket_options()));
      } else {
        Network::Socket::appendOptions(extra_upstream_local_address.socket_options_,
                                       cluster_socket_options);
      }

      upstream_local_addresses_.push_back(extra_upstream_local_address);
    }

    if (bind_config->additional_source_addresses_size() == 1) {
      UpstreamLocalAddress additional_upstream_local_address;
      additional_upstream_local_address.address_ =
          Network::Address::resolveProtoSocketAddress(bind_config->additional_source_addresses(0));
      ASSERT(additional_upstream_local_address.address_->ip() != nullptr &&
             upstream_local_address.address_->ip() != nullptr);
      if (additional_upstream_local_address.address_->ip()->version() ==
          upstream_local_address.address_->ip()->version()) {
        throw EnvoyException(fmt::format(
            "{}'s upstream binding config has two same IP version source addresses. Only two "
            "different IP version source addresses can be supported in BindConfig's source_address "
            "and additional_source_addresses fields",
            !(cluster_name.has_value()) ? "Bootstrap"
                                        : fmt::format("Cluster {}", cluster_name.value())));
      }

      additional_upstream_local_address.socket_options_ =
          std::make_shared<Network::ConnectionSocket::Options>();

      Network::Socket::appendOptions(additional_upstream_local_address.socket_options_,
                                     base_socket_options);
      Network::Socket::appendOptions(additional_upstream_local_address.socket_options_,
                                     cluster_socket_options);

      upstream_local_addresses_.push_back(additional_upstream_local_address);
    }
  } else {
    // If there is no bind config specified, then return a nullptr for the address.
    UpstreamLocalAddress local_address;
    local_address.address_ = nullptr;
    local_address.socket_options_ = std::make_shared<Network::ConnectionSocket::Options>();
    Network::Socket::appendOptions(local_address.socket_options_, base_socket_options);
    Network::Socket::appendOptions(local_address.socket_options_, cluster_socket_options);
    upstream_local_addresses_.push_back(local_address);
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
    connection_options = std::make_shared<Network::ConnectionSocket::Options>();
    *connection_options = *options;
    Network::Socket::appendOptions(connection_options, local_address_options);
  } else {
    *connection_options = *local_address_options;
  }

  return connection_options;
}

} // namespace Upstream
} // namespace Envoy
