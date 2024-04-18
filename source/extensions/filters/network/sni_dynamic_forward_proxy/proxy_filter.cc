#include "source/extensions/filters/network/sni_dynamic_forward_proxy/proxy_filter.h"

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/uint32_accessor.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/assert.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniDynamicForwardProxy {

ProxyFilterConfig::ProxyFilterConfig(
    const FilterConfig& proto_config,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    Upstream::ClusterManager&)
    : port_(static_cast<uint16_t>(proto_config.port_value())),
      dns_cache_manager_(cache_manager_factory.get()) {
  auto cache_or_error = dns_cache_manager_->getCache(proto_config.dns_cache_config());
  THROW_IF_STATUS_NOT_OK(cache_or_error, throw);
  dns_cache_ = std::move(cache_or_error.value());
}

ProxyFilter::ProxyFilter(ProxyFilterConfigSharedPtr config) : config_(std::move(config)) {}

using LoadDnsCacheEntryStatus = Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;

Network::FilterStatus ProxyFilter::onNewConnection() {
  const Router::StringAccessor* dynamic_host_filter_state =
      read_callbacks_->connection()
          .streamInfo()
          .filterState()
          ->getDataReadOnly<Router::StringAccessor>("envoy.upstream.dynamic_host");

  absl::string_view host;
  if (dynamic_host_filter_state) {
    host = dynamic_host_filter_state->asString();
  } else {
    host = read_callbacks_->connection().requestedServerName();
  }

  ENVOY_CONN_LOG(trace, "sni_dynamic_forward_proxy: new connection with server name '{}'",
                 read_callbacks_->connection(), host);

  if (host.empty()) {
    return Network::FilterStatus::Continue;
  }

  circuit_breaker_ = config_->cache().canCreateDnsRequest();

  if (circuit_breaker_ == nullptr) {
    ENVOY_CONN_LOG(debug, "pending request overflow", read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  const StreamInfo::UInt32Accessor* dynamic_port_filter_state =
      read_callbacks_->connection()
          .streamInfo()
          .filterState()
          ->getDataReadOnly<StreamInfo::UInt32Accessor>("envoy.upstream.dynamic_port");

  uint32_t port;
  if (dynamic_port_filter_state != nullptr && dynamic_port_filter_state->value() > 0 &&
      dynamic_port_filter_state->value() <= 65535) {
    port = dynamic_port_filter_state->value();
  } else {
    port = config_->port();
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.dfp_mixed_scheme")) {
      read_callbacks_->connection().streamInfo().filterState()->setData(
          "envoy.upstream.dynamic_port", std::make_shared<StreamInfo::UInt32AccessorImpl>(port),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::Connection);
    }
  }

  auto result = config_->cache().loadDnsCacheEntry(host, port, false, *this);

  cache_load_handle_ = std::move(result.handle_);
  if (cache_load_handle_ == nullptr) {
    circuit_breaker_.reset();
  }

  switch (result.status_) {
  case LoadDnsCacheEntryStatus::InCache:
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_CONN_LOG(debug, "DNS cache entry already loaded, continuing",
                   read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  case LoadDnsCacheEntryStatus::Loading:
    ASSERT(cache_load_handle_ != nullptr);
    ENVOY_CONN_LOG(debug, "waiting to load DNS cache entry", read_callbacks_->connection());
    read_callbacks_->connection().readDisable(true);
    return Network::FilterStatus::StopIteration;
  case LoadDnsCacheEntryStatus::Overflow:
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_CONN_LOG(debug, "DNS cache overflow", read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

void ProxyFilter::onLoadDnsCacheComplete(const Common::DynamicForwardProxy::DnsHostInfoSharedPtr&) {
  ENVOY_CONN_LOG(debug, "load DNS cache complete, continuing", read_callbacks_->connection());
  ASSERT(circuit_breaker_ != nullptr);
  circuit_breaker_.reset();
  read_callbacks_->connection().readDisable(false);
  read_callbacks_->continueReading();
}

} // namespace SniDynamicForwardProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
