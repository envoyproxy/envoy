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

UpstreamLocalAddressSelectorConstSharedPtr
DefaultUpstreamLocalAddressSelectorFactory::createLocalAddressSelector(
    std::vector<::Envoy::Upstream::UpstreamLocalAddress> upstream_local_addresses,
    absl::optional<std::string> cluster_name) const {
  return std::make_shared<DefaultUpstreamLocalAddressSelector>(upstream_local_addresses,
                                                               cluster_name);
}

/**
 * Static registration for the default local address selector. @see RegisterFactory.
 */
REGISTER_FACTORY(DefaultUpstreamLocalAddressSelectorFactory, UpstreamLocalAddressSelectorFactory);

} // namespace Upstream
} // namespace Envoy
