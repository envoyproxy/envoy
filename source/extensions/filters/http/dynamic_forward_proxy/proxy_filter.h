#pragma once

#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"
#include "source/extensions/common/dynamic_forward_proxy/cluster_store.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

/**
 * Handle returned from addDynamicCluster(). Destruction of the handle will cancel any future
 * callback.
 */
class LoadClusterEntryHandle {
public:
  virtual ~LoadClusterEntryHandle() = default;
};

using LoadClusterEntryHandlePtr = std::unique_ptr<LoadClusterEntryHandle>;

class LoadClusterEntryCallbacks {
public:
  virtual ~LoadClusterEntryCallbacks() = default;

  virtual void onLoadClusterComplete() PURE;
};

class ProxyFilterConfig : public Upstream::ClusterUpdateCallbacks,
                          Logger::Loggable<Logger::Id::forward_proxy> {
public:
  ProxyFilterConfig(
      const envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig& proto_config,
      Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr&& cache,
      Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr&& cache_manager,
      Extensions::Common::DynamicForwardProxy::DFPClusterStoreFactory& cluster_store_factory,
      Server::Configuration::FactoryContext& context);

  Extensions::Common::DynamicForwardProxy::DFPClusterStoreSharedPtr clusterStore() {
    return cluster_store_;
  }
  Extensions::Common::DynamicForwardProxy::DnsCache& cache() { return *dns_cache_; }
  Upstream::ClusterManager& clusterManager() { return cluster_manager_; }
  bool saveUpstreamAddress() const { return save_upstream_address_; };
  const std::chrono::milliseconds clusterInitTimeout() const { return cluster_init_timeout_; };

  LoadClusterEntryHandlePtr
  addDynamicCluster(Extensions::Common::DynamicForwardProxy::DfpClusterSharedPtr cluster,
                    const std::string& cluster_name, const std::string& host, const int port,
                    LoadClusterEntryCallbacks& callback);
  // run in each worker thread.
  Upstream::ClusterUpdateCallbacksHandlePtr addThreadLocalClusterUpdateCallbacks();

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand&) override;
  void onClusterRemoval(const std::string&) override;

private:
  struct LoadClusterEntryHandleImpl
      : public LoadClusterEntryHandle,
        RaiiMapOfListElement<std::string, LoadClusterEntryHandleImpl*> {
    LoadClusterEntryHandleImpl(
        absl::flat_hash_map<std::string, std::list<LoadClusterEntryHandleImpl*>>& parent,
        absl::string_view host, LoadClusterEntryCallbacks& callbacks)
        : RaiiMapOfListElement<std::string, LoadClusterEntryHandleImpl*>(parent, host, this),
          callbacks_(callbacks) {}

    LoadClusterEntryCallbacks& callbacks_;
  };

  // Per-thread cluster info including pending callbacks.
  struct ThreadLocalClusterInfo : public ThreadLocal::ThreadLocalObject {
    ThreadLocalClusterInfo(ProxyFilterConfig& parent) : parent_{parent} {
      handle_ = parent.addThreadLocalClusterUpdateCallbacks();
    }
    ~ThreadLocalClusterInfo() override;
    absl::flat_hash_map<std::string, std::list<LoadClusterEntryHandleImpl*>> pending_clusters_;
    ProxyFilterConfig& parent_;
    Upstream::ClusterUpdateCallbacksHandlePtr handle_;
  };

  Extensions::Common::DynamicForwardProxy::DFPClusterStoreSharedPtr cluster_store_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr dns_cache_manager_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_;
  Upstream::ClusterManager& cluster_manager_;
  Event::Dispatcher& main_thread_dispatcher_;
  ThreadLocal::TypedSlot<ThreadLocalClusterInfo> tls_slot_;
  const std::chrono::milliseconds cluster_init_timeout_;
  const bool save_upstream_address_;
};

using ProxyFilterConfigSharedPtr = std::shared_ptr<ProxyFilterConfig>;

class ProxyPerRouteConfig : public ::Envoy::Router::RouteSpecificFilterConfig {
public:
  ProxyPerRouteConfig(
      const envoy::extensions::filters::http::dynamic_forward_proxy::v3::PerRouteConfig& config);

  const std::string& hostRewrite() const { return host_rewrite_; }
  const Http::LowerCaseString& hostRewriteHeader() const { return host_rewrite_header_; }

private:
  const std::string host_rewrite_;
  const Http::LowerCaseString host_rewrite_header_;
};

class ProxyFilter
    : public Http::PassThroughDecoderFilter,
      public Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks,
      public LoadClusterEntryCallbacks,
      Logger::Loggable<Logger::Id::forward_proxy> {
public:
  ProxyFilter(const ProxyFilterConfigSharedPtr& config) : config_(config) {}

  static constexpr absl::string_view DNS_START = "envoy.dynamic_forward_proxy.dns_start_ms";
  static constexpr absl::string_view DNS_END = "envoy.dynamic_forward_proxy.dns_end_ms";

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void onDestroy() override;

  Http::FilterHeadersStatus
  loadDynamicCluster(Extensions::Common::DynamicForwardProxy::DfpClusterSharedPtr cluster,
                     Http::RequestHeaderMap& headers, uint16_t default_port);

  // Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks
  void onLoadDnsCacheComplete(
      const Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr&) override;

  // LoadClusterEntryCallbacks
  void onLoadClusterComplete() override;

  void onClusterInitTimeout();

private:
  void addHostAddressToFilterState(const Network::Address::InstanceConstSharedPtr& address);
  void onDnsResolutionFail();
  bool isProxying();

  const ProxyFilterConfigSharedPtr config_;
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  Upstream::ResourceAutoIncDecPtr circuit_breaker_;
  Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryHandlePtr cache_load_handle_;
  LoadClusterEntryHandlePtr cluster_load_handle_;
  Event::TimerPtr cluster_init_timer_;
};

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
