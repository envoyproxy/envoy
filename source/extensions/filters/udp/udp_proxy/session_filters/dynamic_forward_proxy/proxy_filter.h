#pragma once

#include <queue>

#include "envoy/extensions/filters/udp/udp_proxy/session/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_utility.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicForwardProxy {

using FilterConfig =
    envoy::extensions::filters::udp::udp_proxy::session::dynamic_forward_proxy::v3::FilterConfig;

/**
 * All filter state dynamic forward proxy stats. @see stats_macros.h
 */
#define ALL_DYNAMIC_FORWARD_PROXY_STATS(COUNTER) COUNTER(buffer_overflow)

/**
 * Struct definition for all filter state dynamic forward proxy stats. @see stats_macros.h
 */
struct DynamicForwardProxyStats {
  ALL_DYNAMIC_FORWARD_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

class ProxyFilterConfig {
public:
  ProxyFilterConfig(
      const FilterConfig& config,
      Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
      Server::Configuration::FactoryContext& context);

  Extensions::Common::DynamicForwardProxy::DnsCache& cache() { return *dns_cache_; }
  DynamicForwardProxyStats& filterStats() { return filter_stats_; }
  bool bufferEnabled() const { return buffer_enabled_; };
  void disableBuffer() { buffer_enabled_ = false; }
  uint32_t maxBufferedDatagrams() const { return max_buffered_datagrams_; };
  uint64_t maxBufferedBytes() const { return max_buffered_bytes_; };

private:
  static DynamicForwardProxyStats generateStats(Stats::Scope& scope) {
    return {ALL_DYNAMIC_FORWARD_PROXY_STATS(POOL_COUNTER(scope))};
  }

  const Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr dns_cache_manager_;
  Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_;
  const Stats::ScopeSharedPtr stats_scope_;
  DynamicForwardProxyStats filter_stats_;
  bool buffer_enabled_;
  uint32_t max_buffered_datagrams_;
  uint64_t max_buffered_bytes_;
};

using ProxyFilterConfigSharedPtr = std::shared_ptr<ProxyFilterConfig>;
using BufferedDatagramPtr = std::unique_ptr<Network::UdpRecvData>;
using LoadDnsCacheEntryStatus = Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;

using ReadFilter = Network::UdpSessionReadFilter;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;

class ProxyFilter
    : public ReadFilter,
      public Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks,
      Logger::Loggable<Logger::Id::forward_proxy> {
public:
  ProxyFilter(ProxyFilterConfigSharedPtr config) : config_(std::move(config)){};

  // Network::ReadFilter
  ReadFilterStatus onNewSession() override;
  ReadFilterStatus onData(Network::UdpRecvData& data) override;

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks
  void onLoadDnsCacheComplete(
      const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&) override;

private:
  void maybeBufferDatagram(Network::UdpRecvData& data);

  const ProxyFilterConfigSharedPtr config_;
  Upstream::ResourceAutoIncDecPtr circuit_breaker_;
  Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryHandlePtr cache_load_handle_;
  ReadFilterCallbacks* read_callbacks_{};
  bool load_dns_cache_completed_{false};
  uint64_t buffered_bytes_{0};
  std::queue<BufferedDatagramPtr> datagrams_buffer_;
};

} // namespace DynamicForwardProxy
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
