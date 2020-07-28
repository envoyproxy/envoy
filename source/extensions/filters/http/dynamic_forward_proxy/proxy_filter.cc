#include "extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"

#include "common/runtime/runtime_features.h"

#include "extensions/clusters/well_known_names.h"
#include "extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

struct ResponseStringValues {
  const std::string DnsCacheOverflow = "DNS cache overflow";
  const std::string PendingRequestOverflow = "Dynamic forward proxy pending request overflow";
};

using CustomClusterType = envoy::config::cluster::v3::Cluster::CustomClusterType;

using ResponseStrings = ConstSingleton<ResponseStringValues>;

using LoadDnsCacheEntryStatus = Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;

ProxyFilterConfig::ProxyFilterConfig(
    const envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig& proto_config,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    Upstream::ClusterManager& cluster_manager)
    : dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(proto_config.dns_cache_config())),
      cluster_manager_(cluster_manager) {}

ProxyPerRouteConfig::ProxyPerRouteConfig(
    const envoy::extensions::filters::http::dynamic_forward_proxy::v3::PerRouteConfig& config)
    : host_rewrite_(config.host_rewrite_literal()),
      host_rewrite_header_(Http::LowerCaseString(config.host_rewrite_header())) {}

void ProxyFilter::onDestroy() {
  // Make sure we destroy any active cache load handle in case we are getting reset and deferred
  // deleted.
  cache_load_handle_.reset();
  circuit_breaker_.reset();
}

Http::FilterHeadersStatus ProxyFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  const Router::RouteEntry* route_entry;
  if (!route || !(route_entry = route->routeEntry())) {
    return Http::FilterHeadersStatus::Continue;
  }

  Upstream::ThreadLocalCluster* cluster = config_->clusterManager().get(route_entry->clusterName());
  if (!cluster) {
    return Http::FilterHeadersStatus::Continue;
  }
  cluster_info_ = cluster->info();

  // We only need to do DNS lookups for hosts in dynamic forward proxy clusters,
  // since the other cluster types do their own DNS management.
  const absl::optional<CustomClusterType>& cluster_type = cluster_info_->clusterType();
  if (!cluster_type) {
    return Http::FilterHeadersStatus::Continue;
  }
  if (cluster_type->name() !=
      Envoy::Extensions::Clusters::ClusterTypes::get().DynamicForwardProxy) {
    return Http::FilterHeadersStatus::Continue;
  }

  const bool should_use_dns_cache_circuit_breakers =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_dns_cache_circuit_breakers");

  circuit_breaker_ = config_->cache().canCreateDnsRequest(
      !should_use_dns_cache_circuit_breakers
          ? absl::make_optional(std::reference_wrapper<ResourceLimit>(
                cluster_info_->resourceManager(route_entry->priority()).pendingRequests()))
          : absl::nullopt);

  if (circuit_breaker_ == nullptr) {
    if (!should_use_dns_cache_circuit_breakers) {
      cluster_info_->stats().upstream_rq_pending_overflow_.inc();
    }
    ENVOY_STREAM_LOG(debug, "pending request overflow", *this->decoder_callbacks_);
    this->decoder_callbacks_->sendLocalReply(
        Http::Code::ServiceUnavailable, ResponseStrings::get().PendingRequestOverflow, nullptr,
        absl::nullopt, ResponseStrings::get().PendingRequestOverflow);
    return Http::FilterHeadersStatus::StopIteration;
  }

  uint16_t default_port = 80;
  if (cluster_info_->transportSocketMatcher()
          .resolve(nullptr)
          .factory_.implementsSecureTransport()) {
    default_port = 443;
  }

  // Check for per route filter config.
  const auto* config = route_entry->mostSpecificPerFilterConfigTyped<ProxyPerRouteConfig>(
      HttpFilterNames::get().DynamicForwardProxy);
  if (config != nullptr) {
    const auto& host_rewrite = config->hostRewrite();
    if (!host_rewrite.empty()) {
      headers.setHost(host_rewrite);
    }

    const auto& host_rewrite_header = config->hostRewriteHeader();
    if (!host_rewrite_header.get().empty()) {
      const auto* header = headers.get(host_rewrite_header);
      if (header != nullptr) {
        const auto& header_value = header->value().getStringView();
        headers.setHost(header_value);
      }
    }
  }

  // See the comments in dns_cache.h for how loadDnsCacheEntry() handles hosts with embedded ports.
  // TODO(mattklein123): Because the filter and cluster have independent configuration, it is
  //                     not obvious to the user if something is misconfigured. We should see if
  //                     we can do better here, perhaps by checking the cache to see if anything
  //                     else is attached to it or something else?
  auto result = config_->cache().loadDnsCacheEntry(headers.Host()->value().getStringView(),
                                                   default_port, *this);
  cache_load_handle_ = std::move(result.handle_);
  if (cache_load_handle_ == nullptr) {
    circuit_breaker_.reset();
  }

  switch (result.status_) {
  case LoadDnsCacheEntryStatus::InCache: {
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_STREAM_LOG(debug, "DNS cache entry already loaded, continuing", *decoder_callbacks_);
    return Http::FilterHeadersStatus::Continue;
  }
  case LoadDnsCacheEntryStatus::Loading: {
    ASSERT(cache_load_handle_ != nullptr);
    ENVOY_STREAM_LOG(debug, "waiting to load DNS cache entry", *decoder_callbacks_);
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }
  case LoadDnsCacheEntryStatus::Overflow: {
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_STREAM_LOG(debug, "DNS cache overflow", *decoder_callbacks_);
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable,
                                       ResponseStrings::get().DnsCacheOverflow, nullptr,
                                       absl::nullopt, ResponseStrings::get().DnsCacheOverflow);
    return Http::FilterHeadersStatus::StopIteration;
  }
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void ProxyFilter::onLoadDnsCacheComplete() {
  ENVOY_STREAM_LOG(debug, "load DNS cache complete, continuing", *decoder_callbacks_);
  ASSERT(circuit_breaker_ != nullptr);
  circuit_breaker_.reset();
  decoder_callbacks_->continueDecoding();
}

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
