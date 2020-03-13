#pragma once

#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

class ProxyFilterConfig {
public:
  ProxyFilterConfig(
      const envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig& proto_config,
      Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
      Upstream::ClusterManager& cluster_manager);

  Extensions::Common::DynamicForwardProxy::DnsCache& cache() { return *dns_cache_; }
  Upstream::ClusterManager& clusterManager() { return cluster_manager_; }

private:
  const Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr dns_cache_manager_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_;
  Upstream::ClusterManager& cluster_manager_;
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
      Logger::Loggable<Logger::Id::forward_proxy> {
public:
  ProxyFilter(const ProxyFilterConfigSharedPtr& config) : config_(config) {}

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void onDestroy() override;

  // Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryCallbacks
  void onLoadDnsCacheComplete() override;

private:
  const ProxyFilterConfigSharedPtr config_;
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  Upstream::ResourceAutoIncDecPtr circuit_breaker_;
  Extensions::Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryHandlePtr cache_load_handle_;
};

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
