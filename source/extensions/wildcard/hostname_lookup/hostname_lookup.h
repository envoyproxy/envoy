#pragma once

#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/wildcard/virtual_ip_cache/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace HostnameLookup {

/**
 * TCP filter that looks up hostname for virtual IP and stores it in filter state.
 * This runs on TCP connection establishment, before HTTP processing.
 */
class HostnameLookupFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  HostnameLookupFilter(VirtualIp::VirtualIpCacheManagerSharedPtr cache_manager);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

private:
  Network::ReadFilterCallbacks* read_callbacks_{nullptr};
  VirtualIp::VirtualIpCacheManagerSharedPtr cache_manager_;
  bool lookup_done_{false};
};

} // namespace HostnameLookup
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
