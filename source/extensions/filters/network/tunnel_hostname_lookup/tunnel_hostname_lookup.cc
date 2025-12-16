#include "source/extensions/filters/network/tunnel_hostname_lookup/tunnel_hostname_lookup.h"

#include "source/common/common/assert.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TunnelHostnameLookup {

constexpr absl::string_view DefaultFilterStateKey = "tunnel_hostname";

TunnelHostnameLookupConfig::TunnelHostnameLookupConfig(
    const envoy::extensions::filters::network::tunnel_hostname_lookup::v3::TunnelHostnameLookup&
        proto_config) {
  filter_state_key_ = proto_config.filter_state_key().empty() ? std::string(DefaultFilterStateKey)
                                                              : proto_config.filter_state_key();
}

TunnelHostnameLookupFilter::TunnelHostnameLookupFilter(
    TunnelHostnameLookupConfigSharedPtr config,
    Common::SyntheticIp::SyntheticIpCacheManagerSharedPtr cache_manager)
    : config_(config), cache_manager_(cache_manager) {}

Network::FilterStatus TunnelHostnameLookupFilter::onNewConnection() {
  // Perform hostname lookup once when connection is established
  if (lookup_performed_) {
    return Network::FilterStatus::Continue;
  }
  lookup_performed_ = true;

  // Get the destination address from the connection
  const auto& connection_info = read_callbacks_->connection().connectionInfoProvider();

  const auto local_address = connection_info.localAddress();
  if (!local_address || !local_address->ip()) {
    ENVOY_LOG(debug, "No local IP address available");
    return Network::FilterStatus::Continue;
  }

  // The destination IP is the local address from the connection's perspective
  const std::string destination_ip = local_address->ip()->addressAsString();
  ENVOY_LOG(debug, "Looking up synthetic IP: {}", destination_ip);

  // Lookup the hostname in the cache
  auto cache_entry = cache_manager_->lookup(destination_ip);
  if (!cache_entry.has_value()) {
    ENVOY_LOG(debug, "No cache entry found for IP: {}", destination_ip);
    return Network::FilterStatus::Continue;
  }

  const std::string& hostname = cache_entry->hostname;
  ENVOY_LOG(info, "Found hostname {} for synthetic IP {}", hostname, destination_ip);

  // Store the hostname in filter state so downstream filters can use it
  // We use StreamInfo filter state which is accessible by all filters in the chain
  auto& filter_state = read_callbacks_->connection().streamInfo().filterState();
  filter_state->setData(
      config_->filterStateKey(), std::make_shared<Router::StringAccessorImpl>(hostname),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);

  ENVOY_LOG(debug, "Stored hostname in filter state with key: {}", config_->filterStateKey());

  return Network::FilterStatus::Continue;
}

Network::FilterStatus TunnelHostnameLookupFilter::onData(Buffer::Instance&, bool) {
  // We only need to perform the lookup once on new connection
  // No need to process data
  return Network::FilterStatus::Continue;
}

void TunnelHostnameLookupFilter::initializeReadFilterCallbacks(
    Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

} // namespace TunnelHostnameLookup
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
