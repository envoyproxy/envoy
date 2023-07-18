#include "source/extensions/upstream/local_address_selector/config.h"

#include "source/extensions/upstream/local_address_selector/default_local_address_selector.h"

namespace Envoy {
namespace Upstream {
namespace {
constexpr absl::string_view kDefaultLocalAddressSelectorName =
    "envoy.upstream.local_address_selector.default_local_address_selector";
}

std::string DefaultUpstreamLocalAddressSelectorFactory::name() const {
  return std::string(kDefaultLocalAddressSelectorName);
}

UpstreamLocalAddressSelectorPtr
DefaultUpstreamLocalAddressSelectorFactory::createLocalAddressSelector(
    ::Envoy::OptRef<const envoy::config::core::v3::BindConfig> bind_config,
    Network::ConnectionSocket::OptionsSharedPtr base_socket_options,
    Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options,
    absl::optional<std::string> cluster_name) const {
  return std::make_shared<DefaultUpstreamLocalAddressSelector>(
      bind_config, base_socket_options, cluster_socket_options, cluster_name);
}

/**
 * Static registration for the default local address selector. @see RegisterFactory.
 */
REGISTER_FACTORY(DefaultUpstreamLocalAddressSelectorFactory, UpstreamLocalAddressSelectorFactory);

} // namespace Upstream
} // namespace Envoy
