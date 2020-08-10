#include "extensions/filters/network/sni_dynamic_forward_proxy/proxy_filter.h"

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "common/common/assert.h"
#include "common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniDynamicForwardProxy {

ProxyFilterConfig::ProxyFilterConfig(
    const FilterConfig& proto_config,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    Upstream::ClusterManager&)
    : port_(static_cast<uint16_t>(proto_config.port_value())),
      dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(proto_config.dns_cache_config())) {}

ProxyFilter::ProxyFilter(ProxyFilterConfigSharedPtr config) : config_(std::move(config)) {}

using LoadDnsCacheEntryStatus = Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;

Network::FilterStatus ProxyFilter::onNewConnection() {
  absl::string_view sni = read_callbacks_->connection().requestedServerName();
  ENVOY_CONN_LOG(trace, "sni_dynamic_forward_proxy: new connection with server name '{}'",
                 read_callbacks_->connection(), sni);

  if (sni.empty()) {
    return Network::FilterStatus::Continue;
  }

  circuit_breaker_ = config_->cache().canCreateDnsRequest(absl::nullopt);

  if (circuit_breaker_ == nullptr) {
    ENVOY_CONN_LOG(debug, "pending request overflow", read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  uint32_t default_port = config_->port();

  auto result = config_->cache().loadDnsCacheEntry(sni, default_port, *this);

  cache_load_handle_ = std::move(result.handle_);
  if (cache_load_handle_ == nullptr) {
    circuit_breaker_.reset();
  }

  switch (result.status_) {
  case LoadDnsCacheEntryStatus::InCache: {
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_CONN_LOG(debug, "DNS cache entry already loaded, continuing",
                   read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  }
  case LoadDnsCacheEntryStatus::Loading: {
    ASSERT(cache_load_handle_ != nullptr);
    ENVOY_CONN_LOG(debug, "waiting to load DNS cache entry", read_callbacks_->connection());
    return Network::FilterStatus::StopIteration;
  }
  case LoadDnsCacheEntryStatus::Overflow: {
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_CONN_LOG(debug, "DNS cache overflow", read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void ProxyFilter::onLoadDnsCacheComplete() {
  ENVOY_CONN_LOG(debug, "load DNS cache complete, continuing", read_callbacks_->connection());
  ASSERT(circuit_breaker_ != nullptr);
  circuit_breaker_.reset();
  read_callbacks_->continueReading();
}

} // namespace SniDynamicForwardProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
