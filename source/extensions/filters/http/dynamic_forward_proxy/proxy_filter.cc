#include "extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

ProxyFilterConfig::ProxyFilterConfig(
    const envoy::config::filter::http::dynamic_forward_proxy::v2alpha::FilterConfig& proto_config,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    Upstream::ClusterManager& cluster_manager)
    : dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(proto_config.dns_cache_config())),
      cluster_manager_(cluster_manager) {}

void ProxyFilter::onDestroy() {
  // Make sure we destroy any active cache load handle in case we are getting reset and deferred
  // deleted.
  cache_load_handle_.reset();
}

Http::FilterHeadersStatus ProxyFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  if (!route || !route->routeEntry()) {
    return Http::FilterHeadersStatus::Continue;
  }

  Upstream::ThreadLocalCluster* cluster =
      config_->clusterManager().get(route->routeEntry()->clusterName());
  if (!cluster) {
    return Http::FilterHeadersStatus::Continue;
  }

  uint16_t default_port = 80;
  if (cluster->info()->transportSocketFactory().implementsSecureTransport()) {
    default_port = 443;
  }

  // See the comments in dns_cache.h for how loadDnsCache() handles hosts with embedded ports.
  // TODO(mattklein123): Because the filter and cluster have independent configuration, it is
  //                     not obvious to the user if something is misconfigured. We should see if
  //                     we can do better here, perhaps by checking the cache to see if anything
  //                     else is attached to it or something else?
  cache_load_handle_ =
      config_->cache().loadDnsCache(headers.Host()->value().getStringView(), default_port, *this);
  if (cache_load_handle_ == nullptr) {
    ENVOY_STREAM_LOG(debug, "DNS cache already loaded, continuing", *decoder_callbacks_);
    return Http::FilterHeadersStatus::Continue;
  }

  ENVOY_STREAM_LOG(debug, "waiting to load DNS cache", *decoder_callbacks_);
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

void ProxyFilter::onLoadDnsCacheComplete() {
  ENVOY_STREAM_LOG(debug, "load DNS cache complete, continuing", *decoder_callbacks_);
  decoder_callbacks_->continueDecoding();
}

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
