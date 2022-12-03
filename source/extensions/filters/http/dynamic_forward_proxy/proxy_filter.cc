#include "source/extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"

#include "source/common/http/utility.h"
#include "source/common/network/filter_state_proxy_info.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/upstream_address.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {
namespace {

void latchTime(Http::StreamDecoderFilterCallbacks* decoder_callbacks, absl::string_view key) {
  StreamInfo::DownstreamTiming& downstream_timing =
      decoder_callbacks->streamInfo().downstreamTiming();
  downstream_timing.setValue(key, decoder_callbacks->dispatcher().timeSource().monotonicTime());
}

} // namespace
struct ResponseStringValues {
  const std::string DnsCacheOverflow = "DNS cache overflow";
  const std::string PendingRequestOverflow = "Dynamic forward proxy pending request overflow";
  const std::string DnsResolutionFailure = "DNS resolution failure";
};

struct RcDetailsValues {
  const std::string DnsCacheOverflow = "dns_cache_overflow";
  const std::string PendingRequestOverflow = "dynamic_forward_proxy_pending_request_overflow";
  const std::string DnsResolutionFailure = "dns_resolution_failure";
};

using CustomClusterType = envoy::config::cluster::v3::Cluster::CustomClusterType;

using ResponseStrings = ConstSingleton<ResponseStringValues>;
using RcDetails = ConstSingleton<RcDetailsValues>;

using LoadDnsCacheEntryStatus = Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;

ProxyFilterConfig::ProxyFilterConfig(
    const envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig& proto_config,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    Upstream::ClusterManager& cluster_manager)
    : dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(proto_config.dns_cache_config())),
      cluster_manager_(cluster_manager),
      save_upstream_address_(proto_config.save_upstream_address()) {}

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

bool ProxyFilter::isProxying() {
  if (!(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.skip_dns_lookup_for_proxied_requests"))) {
    return false;
  }
  const Envoy::StreamInfo::FilterStateSharedPtr& filter_state =
      decoder_callbacks_->streamInfo().filterState();
  return filter_state && filter_state->hasData<Network::Http11ProxyInfoFilterState>(
                             Network::Http11ProxyInfoFilterState::key());
}

Http::FilterHeadersStatus ProxyFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  const Router::RouteEntry* route_entry;
  if (!route || !(route_entry = route->routeEntry())) {
    return Http::FilterHeadersStatus::Continue;
  }

  Upstream::ThreadLocalCluster* cluster =
      config_->clusterManager().getThreadLocalCluster(route_entry->clusterName());
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

  if (cluster_type->name() != "envoy.clusters.dynamic_forward_proxy") {
    ENVOY_STREAM_LOG(debug, "cluster_type->name(): {} ", *this->decoder_callbacks_,
                     cluster_type->name());
    return Http::FilterHeadersStatus::Continue;
  }

  circuit_breaker_ = config_->cache().canCreateDnsRequest();

  if (circuit_breaker_ == nullptr) {
    ENVOY_STREAM_LOG(debug, "pending request overflow", *this->decoder_callbacks_);
    this->decoder_callbacks_->sendLocalReply(
        Http::Code::ServiceUnavailable, ResponseStrings::get().PendingRequestOverflow, nullptr,
        absl::nullopt, RcDetails::get().PendingRequestOverflow);
    return Http::FilterHeadersStatus::StopIteration;
  }

  uint16_t default_port = 80;
  if (cluster_info_->transportSocketMatcher()
          .resolve(nullptr)
          .factory_.implementsSecureTransport()) {
    default_port = 443;
  }

  // Check for per route filter config.
  const auto* config =
      Http::Utility::resolveMostSpecificPerFilterConfig<ProxyPerRouteConfig>(decoder_callbacks_);

  if (config != nullptr) {
    const auto& host_rewrite = config->hostRewrite();
    if (!host_rewrite.empty()) {
      headers.setHost(host_rewrite);
    }

    const auto& host_rewrite_header = config->hostRewriteHeader();
    if (!host_rewrite_header.get().empty()) {
      const auto header = headers.get(host_rewrite_header);
      if (!header.empty()) {
        // This is an implicitly untrusted header, so per the API documentation only the first
        // value is used.
        const auto& header_value = header[0]->value().getStringView();
        headers.setHost(header_value);
      }
    }
  }

  latchTime(decoder_callbacks_, DNS_START);
  // See the comments in dns_cache.h for how loadDnsCacheEntry() handles hosts with embedded ports.
  // TODO(mattklein123): Because the filter and cluster have independent configuration, it is
  //                     not obvious to the user if something is misconfigured. We should see if
  //                     we can do better here, perhaps by checking the cache to see if anything
  //                     else is attached to it or something else?
  auto result = config_->cache().loadDnsCacheEntry(headers.Host()->value().getStringView(),
                                                   default_port, isProxying(), *this);
  cache_load_handle_ = std::move(result.handle_);
  if (cache_load_handle_ == nullptr) {
    circuit_breaker_.reset();
  }

  switch (result.status_) {
  case LoadDnsCacheEntryStatus::InCache: {
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_STREAM_LOG(debug, "DNS cache entry already loaded, continuing", *decoder_callbacks_);

    auto const& host = config_->cache().getHost(headers.Host()->value().getStringView());
    latchTime(decoder_callbacks_, DNS_END);
    if (!host.has_value() || !host.value()->address()) {
      onDnsResolutionFail();
      return Http::FilterHeadersStatus::StopIteration;
    }
    addHostAddressToFilterState(host.value()->address());

    return Http::FilterHeadersStatus::Continue;
  }
  case LoadDnsCacheEntryStatus::Loading:
    ASSERT(cache_load_handle_ != nullptr);
    ENVOY_STREAM_LOG(debug, "waiting to load DNS cache entry", *decoder_callbacks_);
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  case LoadDnsCacheEntryStatus::Overflow:
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_STREAM_LOG(debug, "DNS cache overflow", *decoder_callbacks_);
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable,
                                       ResponseStrings::get().DnsCacheOverflow, nullptr,
                                       absl::nullopt, RcDetails::get().DnsCacheOverflow);
    return Http::FilterHeadersStatus::StopIteration;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

void ProxyFilter::addHostAddressToFilterState(
    const Network::Address::InstanceConstSharedPtr& address) {
  ASSERT(address); // null pointer checks must be done before calling this function.

  if (!config_->saveUpstreamAddress()) {
    return;
  }

  ENVOY_STREAM_LOG(trace, "Adding resolved host {} to filter state", *decoder_callbacks_,
                   address->asString());

  const Envoy::StreamInfo::FilterStateSharedPtr& filter_state =
      decoder_callbacks_->streamInfo().filterState();

  auto address_obj = std::make_unique<StreamInfo::UpstreamAddress>();
  address_obj->address_ = address;

  filter_state->setData(StreamInfo::UpstreamAddress::key(), std::move(address_obj),
                        StreamInfo::FilterState::StateType::Mutable,
                        StreamInfo::FilterState::LifeSpan::Request);
}

void ProxyFilter::onDnsResolutionFail() {
  if (isProxying()) {
    decoder_callbacks_->continueDecoding();
    return;
  }

  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::DnsResolutionFailed);
  decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable,
                                     ResponseStrings::get().DnsResolutionFailure, nullptr,
                                     absl::nullopt, RcDetails::get().DnsResolutionFailure);
}

void ProxyFilter::onLoadDnsCacheComplete(
    const Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {
  ENVOY_STREAM_LOG(debug, "load DNS cache complete, continuing after adding resolved host: {}",
                   *decoder_callbacks_, host_info->resolvedHost());
  latchTime(decoder_callbacks_, DNS_END);
  ASSERT(circuit_breaker_ != nullptr);
  circuit_breaker_.reset();

  if (!host_info->address()) {
    onDnsResolutionFail();
    return;
  }
  addHostAddressToFilterState(host_info->address());

  decoder_callbacks_->continueDecoding();
}

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
