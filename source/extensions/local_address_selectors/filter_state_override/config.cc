#include "source/extensions/local_address_selectors/filter_state_override/config.h"

#include "envoy/registry/registry.h"
#include "envoy/upstream/upstream.h"

#include "source/common/upstream/default_local_address_selector_factory.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace FilterStateOverride {

namespace {

absl::optional<std::string> getObjectAsString(const StreamInfo::FilterState::Objects& objects,
                                              absl::string_view name) {
  for (const auto& obj : objects) {
    if (obj.name_ == name) {
      return obj.data_->serializeAsString();
    }
  }
  return {};
}

class NamespaceLocalAddressSelector : public Upstream::UpstreamLocalAddressSelector,
                                      public Logger::Loggable<Logger::Id::upstream> {
public:
  explicit NamespaceLocalAddressSelector(
      const Upstream::UpstreamLocalAddressSelectorConstSharedPtr& inner)
      : inner_(inner) {}
  Upstream::UpstreamLocalAddress getUpstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& endpoint_address,
      const Network::ConnectionSocket::OptionsSharedPtr& socket_options,
      OptRef<const Network::TransportSocketOptions> transport_socket_options) const {
    const auto upstream_address =
        inner_->getUpstreamLocalAddress(endpoint_address, socket_options, transport_socket_options);
    if (transport_socket_options && upstream_address.address_) {
      const auto data =
          getObjectAsString(transport_socket_options->downstreamSharedFilterStateObjects(),
                            "envoy.network.upstream_bind_override.network_namespace");
      if (data.has_value()) {
        const auto new_address = upstream_address.address_->withNetworkNamespace(*data);
        if (new_address) {
          return {.address_ = new_address, .socket_options_ = upstream_address.socket_options_};
        }
      } else {
        ENVOY_LOG(trace, "Failed to serialize filter state as string");
      }
    }
    return upstream_address;
  }

private:
  const Upstream::UpstreamLocalAddressSelectorConstSharedPtr inner_;
};

} // namespace
absl::StatusOr<Upstream::UpstreamLocalAddressSelectorConstSharedPtr>
NamespaceLocalAddressSelectorFactory::createLocalAddressSelector(
    std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses,
    absl::optional<std::string> cluster_name) const {
  auto* default_factory =
      Registry::FactoryRegistry<Upstream::UpstreamLocalAddressSelectorFactory>::getFactory(
          "envoy.upstream.local_address_selector.default_local_address_selector");
  ASSERT(default_factory != nullptr);
  const auto default_selector =
      default_factory->createLocalAddressSelector(upstream_local_addresses, cluster_name);
  if (!default_selector.ok()) {
    return default_selector;
  }
  return std::make_shared<const NamespaceLocalAddressSelector>(default_selector.value());
}

REGISTER_FACTORY(NamespaceLocalAddressSelectorFactory,
                 Upstream::UpstreamLocalAddressSelectorFactory);

} // namespace FilterStateOverride
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
