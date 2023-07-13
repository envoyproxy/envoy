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
    const envoy::config::cluster::v3::Cluster& cluster_config,
    const absl::optional<envoy::config::core::v3::BindConfig>& bootstrap_bind_config) const {
  return std::make_shared<DefaultUpstreamLocalAddressSelector>(cluster_config,
                                                               bootstrap_bind_config);
}

/**
 * Static registration for the default local address selector. @see RegisterFactory.
 */
REGISTER_FACTORY(DefaultUpstreamLocalAddressSelectorFactory, UpstreamLocalAddressSelectorFactory);

} // namespace Upstream
} // namespace Envoy
