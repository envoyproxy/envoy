#pragma once

#include "envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3alpha/sni_dynamic_forward_proxy.pb.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniDynamicForwardProxy {

using FilterConfig =
    envoy::extensions::filters::network::sni_dynamic_forward_proxy::v3alpha::FilterConfig;

class ProxyFilterConfig {
public:
  ProxyFilterConfig(
      const FilterConfig& proto_config,
      Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
      Upstream::ClusterManager& cluster_manager);

  Extensions::Common::DynamicForwardProxy::DnsCache& cache() { return *dns_cache_; }
  uint32_t port() { return port_; }

private:
  const uint32_t port_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr dns_cache_manager_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_;
};

using ProxyFilterConfigSharedPtr = std::shared_ptr<ProxyFilterConfig>;

class ProxyFilter
    : public Network::ReadFilter,
      public Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks,
      Logger::Loggable<Logger::Id::forward_proxy> {
public:
  ProxyFilter(ProxyFilterConfigSharedPtr config);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks
  void onLoadDnsCacheComplete() override;

private:
  const ProxyFilterConfigSharedPtr config_;
  Upstream::ResourceAutoIncDecPtr circuit_breaker_;
  Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryHandlePtr cache_load_handle_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace SniDynamicForwardProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
