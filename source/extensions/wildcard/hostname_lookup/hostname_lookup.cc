#include "source/extensions/wildcard/hostname_lookup/hostname_lookup.h"

#include "envoy/network/connection.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/utility.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace HostnameLookup {

// Filter state key for storing the hostname
constexpr absl::string_view HostnameKey = "envoy.wildcard.hostname";

HostnameLookupFilter::HostnameLookupFilter(VirtualIp::VirtualIpCacheManagerSharedPtr cache_manager)
    : cache_manager_(cache_manager) {}

void HostnameLookupFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus HostnameLookupFilter::onNewConnection() {
  if (lookup_done_) {
    return Network::FilterStatus::Continue;
  }

  // Get destination IP
  const auto& local_address = read_callbacks_->connection().connectionInfoProvider().localAddress();
  const auto& remote_address =
      read_callbacks_->connection().connectionInfoProvider().remoteAddress();

  ENVOY_LOG(info, "New connection: remote={}, local={}", remote_address->asString(),
            local_address->asString());

  if (!local_address || !local_address->ip()) {
    ENVOY_LOG(debug, "No IP address available for hostname lookup");
    lookup_done_ = true;
    return Network::FilterStatus::Continue;
  }

  std::string virtual_ip = local_address->ip()->addressAsString();
  ENVOY_LOG(info, "Looking up hostname for virtual IP: {}", virtual_ip);

  // Lookup in cache
  auto hostname = cache_manager_->lookup(virtual_ip);
  if (!hostname.has_value()) {
    ENVOY_LOG(warn, "No hostname found for virtual IP: {} (cache miss)", virtual_ip);
    lookup_done_ = true;
    return Network::FilterStatus::Continue;
  }

  ENVOY_LOG(info, "✓ Found hostname {} for virtual IP {}", hostname.value(), virtual_ip);

  // Store hostname in filter state
  read_callbacks_->connection().streamInfo().filterState()->setData(
      HostnameKey, std::make_unique<Router::StringAccessorImpl>(hostname.value()),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);

  lookup_done_ = true;
  return Network::FilterStatus::Continue;
}

Network::FilterStatus HostnameLookupFilter::onData(Buffer::Instance&, bool) {
  // We only do lookup on new connection, not on data
  return Network::FilterStatus::Continue;
}

} // namespace HostnameLookup
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
