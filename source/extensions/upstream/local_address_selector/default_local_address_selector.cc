#include "source/extensions/upstream/local_address_selector/default_local_address_selector.h"

#include <string>

#include "envoy/network/socket.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/resolver_impl.h"

namespace Envoy {
namespace Upstream {
namespace {

Network::TcpKeepaliveConfig
parseTcpKeepaliveConfig(const envoy::config::cluster::v3::Cluster& config) {
  const envoy::config::core::v3::TcpKeepalive& options =
      config.upstream_connection_options().tcp_keepalive();
  return Network::TcpKeepaliveConfig{
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_probes, absl::optional<uint32_t>()),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_time, absl::optional<uint32_t>()),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_interval, absl::optional<uint32_t>())};
}

} // namespace

DefaultUpstreamLocalAddressSelector::DefaultUpstreamLocalAddressSelector(
    const envoy::config::cluster::v3::Cluster& cluster_config,
    const absl::optional<envoy::config::core::v3::BindConfig>& bootstrap_bind_config) {
  base_socket_options_ = buildBaseSocketOptions(
      cluster_config, bootstrap_bind_config.value_or(envoy::config::core::v3::BindConfig{}));
  cluster_socket_options_ = buildClusterSocketOptions(
      cluster_config, bootstrap_bind_config.value_or(envoy::config::core::v3::BindConfig{}));

  ASSERT(base_socket_options_ != nullptr);
  ASSERT(cluster_socket_options_ != nullptr);

  if (cluster_config.has_upstream_bind_config()) {
    parseBindConfig(cluster_config.name(), cluster_config.upstream_bind_config());
  } else if (bootstrap_bind_config.has_value()) {
    parseBindConfig("", *bootstrap_bind_config);
  }
}

UpstreamLocalAddress DefaultUpstreamLocalAddressSelector::getUpstreamLocalAddress(
    const Network::Address::InstanceConstSharedPtr& endpoint_address,
    const Network::ConnectionSocket::OptionsSharedPtr& socket_options) const {
  // If there is no upstream local address specified, then return a nullptr for the address. And
  // return the socket options.
  if (upstream_local_addresses_.empty()) {
    UpstreamLocalAddress local_address;
    local_address.address_ = nullptr;
    local_address.socket_options_ = std::make_shared<Network::ConnectionSocket::Options>();
    Network::Socket::appendOptions(local_address.socket_options_, base_socket_options_);
    Network::Socket::appendOptions(local_address.socket_options_, cluster_socket_options_);
    local_address.socket_options_ =
        combineConnectionSocketOptions(local_address.socket_options_, socket_options);
    return local_address;
  }

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

void DefaultUpstreamLocalAddressSelector::parseBindConfig(
    const std::string cluster_name, const envoy::config::core::v3::BindConfig& bind_config) {
  if (bind_config.additional_source_addresses_size() > 0 &&
      bind_config.extra_source_addresses_size() > 0) {
    throw EnvoyException(
        fmt::format("Can't specify both `extra_source_addresses` and `additional_source_addresses` "
                    "in the {}'s upstream binding config",
                    cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
  }

  if (bind_config.extra_source_addresses_size() > 1) {
    throw EnvoyException(fmt::format(
        "{}'s upstream binding config has more than one extra source addresses. Only one "
        "extra source can be supported in BindConfig's extra_source_addresses field",
        cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
  }

  if (bind_config.additional_source_addresses_size() > 1) {
    throw EnvoyException(fmt::format(
        "{}'s upstream binding config has more than one additional source addresses. Only one "
        "additional source can be supported in BindConfig's additional_source_addresses field",
        cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
  }

  if (!bind_config.has_source_address() && (bind_config.extra_source_addresses_size() > 0 ||
                                            bind_config.additional_source_addresses_size() > 0)) {
    throw EnvoyException(
        fmt::format("{}'s upstream binding config has extra/additional source addresses but no "
                    "source_address. Extra/additional addresses cannot be specified if "
                    "source_address is not set.",
                    cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
  }

  UpstreamLocalAddress upstream_local_address;
  upstream_local_address.address_ =
      bind_config.has_source_address()
          ? Network::Address::resolveProtoSocketAddress(bind_config.source_address())
          : nullptr;
  upstream_local_address.socket_options_ = std::make_shared<Network::ConnectionSocket::Options>();

  Network::Socket::appendOptions(upstream_local_address.socket_options_, base_socket_options_);
  Network::Socket::appendOptions(upstream_local_address.socket_options_, cluster_socket_options_);

  upstream_local_addresses_.push_back(upstream_local_address);

  if (bind_config.extra_source_addresses_size() == 1) {
    UpstreamLocalAddress extra_upstream_local_address;
    extra_upstream_local_address.address_ = Network::Address::resolveProtoSocketAddress(
        bind_config.extra_source_addresses(0).address());
    ASSERT(extra_upstream_local_address.address_->ip() != nullptr &&
           upstream_local_address.address_->ip() != nullptr);
    if (extra_upstream_local_address.address_->ip()->version() ==
        upstream_local_address.address_->ip()->version()) {
      throw EnvoyException(fmt::format(
          "{}'s upstream binding config has two same IP version source addresses. Only two "
          "different IP version source addresses can be supported in BindConfig's source_address "
          "and extra_source_addresses fields",
          cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
    }

    extra_upstream_local_address.socket_options_ =
        std::make_shared<Network::ConnectionSocket::Options>();
    Network::Socket::appendOptions(extra_upstream_local_address.socket_options_,
                                   base_socket_options_);

    if (bind_config.extra_source_addresses(0).has_socket_options()) {
      Network::Socket::appendOptions(
          extra_upstream_local_address.socket_options_,
          Network::SocketOptionFactory::buildLiteralOptions(
              bind_config.extra_source_addresses(0).socket_options().socket_options()));
    } else {
      Network::Socket::appendOptions(extra_upstream_local_address.socket_options_,
                                     cluster_socket_options_);
    }

    upstream_local_addresses_.push_back(extra_upstream_local_address);
  }

  if (bind_config.additional_source_addresses_size() == 1) {
    UpstreamLocalAddress additional_upstream_local_address;
    additional_upstream_local_address.address_ =
        Network::Address::resolveProtoSocketAddress(bind_config.additional_source_addresses(0));
    ASSERT(additional_upstream_local_address.address_->ip() != nullptr &&
           upstream_local_address.address_->ip() != nullptr);
    if (additional_upstream_local_address.address_->ip()->version() ==
        upstream_local_address.address_->ip()->version()) {
      throw EnvoyException(fmt::format(
          "{}'s upstream binding config has two same IP version source addresses. Only two "
          "different IP version source addresses can be supported in BindConfig's source_address "
          "and additional_source_addresses fields",
          cluster_name.empty() ? "Bootstrap" : fmt::format("Cluster {}", cluster_name)));
    }

    additional_upstream_local_address.socket_options_ =
        std::make_shared<Network::ConnectionSocket::Options>();

    Network::Socket::appendOptions(additional_upstream_local_address.socket_options_,
                                   base_socket_options_);
    Network::Socket::appendOptions(additional_upstream_local_address.socket_options_,
                                   cluster_socket_options_);

    upstream_local_addresses_.push_back(additional_upstream_local_address);
  }
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

Network::ConnectionSocket::OptionsSharedPtr
buildBaseSocketOptions(const envoy::config::cluster::v3::Cluster& cluster_config,
                       const envoy::config::core::v3::BindConfig& bootstrap_bind_config) {
  Network::ConnectionSocket::OptionsSharedPtr base_options =
      std::make_shared<Network::ConnectionSocket::Options>();

  // The process-wide `signal()` handling may fail to handle SIGPIPE if overridden
  // in the process (i.e., on a mobile client). Some OSes support handling it at the socket layer:
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    Network::Socket::appendOptions(base_options,
                                   Network::SocketOptionFactory::buildSocketNoSigpipeOptions());
  }
  // Cluster IP_FREEBIND settings, when set, will override the cluster manager wide settings.
  if ((bootstrap_bind_config.freebind().value() &&
       !cluster_config.upstream_bind_config().has_freebind()) ||
      cluster_config.upstream_bind_config().freebind().value()) {
    Network::Socket::appendOptions(base_options,
                                   Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (cluster_config.upstream_connection_options().has_tcp_keepalive()) {
    Network::Socket::appendOptions(base_options,
                                   Network::SocketOptionFactory::buildTcpKeepaliveOptions(
                                       parseTcpKeepaliveConfig(cluster_config)));
  }

  return base_options;
}

Network::ConnectionSocket::OptionsSharedPtr
buildClusterSocketOptions(const envoy::config::cluster::v3::Cluster& cluster_config,
                          const envoy::config::core::v3::BindConfig& bind_config) {
  Network::ConnectionSocket::OptionsSharedPtr cluster_options =
      std::make_shared<Network::ConnectionSocket::Options>();
  // Cluster socket_options trump cluster manager wide.
  if (bind_config.socket_options().size() +
          cluster_config.upstream_bind_config().socket_options().size() >
      0) {
    auto socket_options = !cluster_config.upstream_bind_config().socket_options().empty()
                              ? cluster_config.upstream_bind_config().socket_options()
                              : bind_config.socket_options();
    Network::Socket::appendOptions(
        cluster_options, Network::SocketOptionFactory::buildLiteralOptions(socket_options));
  }
  return cluster_options;
}

} // namespace Upstream
} // namespace Envoy
