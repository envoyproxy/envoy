#pragma once

#include "envoy/extensions/filters/network/tunnel_hostname_lookup/v3/tunnel_hostname_lookup.pb.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/common/synthetic_ip/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TunnelHostnameLookup {

/**
 * Configuration for the Tunnel Hostname Lookup filter.
 */
class TunnelHostnameLookupConfig {
public:
  TunnelHostnameLookupConfig(
      const envoy::extensions::filters::network::tunnel_hostname_lookup::v3::TunnelHostnameLookup&
          proto_config);

  const std::string& filterStateKey() const { return filter_state_key_; }

private:
  std::string filter_state_key_;
};

using TunnelHostnameLookupConfigSharedPtr = std::shared_ptr<TunnelHostnameLookupConfig>;

/**
 * Tunnel Hostname Lookup filter - retrieves original hostname from synthetic IP cache.
 *
 * This filter intercepts TCP connections to synthetic IPs, looks up the destination
 * IP in the cache to retrieve the original hostname, and stores it in filter state
 * for downstream filters (like HTTP CONNECT proxy) to use.
 */
class TunnelHostnameLookupFilter : public Network::ReadFilter,
                                   Logger::Loggable<Logger::Id::filter> {
public:
  TunnelHostnameLookupFilter(TunnelHostnameLookupConfigSharedPtr config,
                             Common::SyntheticIp::SyntheticIpCacheManagerSharedPtr cache_manager);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

private:
  TunnelHostnameLookupConfigSharedPtr config_;
  Common::SyntheticIp::SyntheticIpCacheManagerSharedPtr cache_manager_;
  Network::ReadFilterCallbacks* read_callbacks_{nullptr};
  bool lookup_performed_{false};
};

} // namespace TunnelHostnameLookup
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
