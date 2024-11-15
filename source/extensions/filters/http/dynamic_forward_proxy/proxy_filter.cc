#include "source/extensions/filters/http/dynamic_forward_proxy/proxy_filter.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"

#include "source/common/http/utility.h"
#include "source/common/network/filter_state_proxy_info.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/upstream_address.h"
#include "source/extensions/common/dynamic_forward_proxy/cluster_store.h"

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
  const std::string SubClusterOverflow = "Sub cluster overflow";
  const std::string SubClusterWarmingTimeout = "Sub cluster warming timeout";
  const std::string DFPClusterIsGone = "Dynamic forward proxy cluster is gone";
};

struct RcDetailsValues {
  const std::string DnsCacheOverflow = "dns_cache_overflow";
  const std::string PendingRequestOverflow = "dynamic_forward_proxy_pending_request_overflow";
  const std::string DnsResolutionFailure = "dns_resolution_failure";
  const std::string SubClusterOverflow = "sub_cluster_overflow";
  const std::string SubClusterWarmingTimeout = "sub_cluster_warming_timeout";
  const std::string DFPClusterIsGone = "dynamic_forward_proxy_cluster_is_gone";
};

using CustomClusterType = envoy::config::cluster::v3::Cluster::CustomClusterType;

using ResponseStrings = ConstSingleton<ResponseStringValues>;
using RcDetails = ConstSingleton<RcDetailsValues>;

using LoadDnsCacheEntryStatus = Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus;

ProxyFilterConfig::ProxyFilterConfig(
    const envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig& proto_config,
    Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr&& cache,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr&& cache_manager,
    Extensions::Common::DynamicForwardProxy::DFPClusterStoreFactory& cluster_store_factory,
    Server::Configuration::FactoryContext& context)
    : cluster_store_(cluster_store_factory.get()), dns_cache_manager_(std::move(cache_manager)),
      dns_cache_(std::move(cache)),
      cluster_manager_(context.serverFactoryContext().clusterManager()),
      main_thread_dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      tls_slot_(context.serverFactoryContext().threadLocal()),
      cluster_init_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(proto_config.sub_cluster_config(),
                                                       cluster_init_timeout, 5000)),
      save_upstream_address_(proto_config.save_upstream_address()) {
  tls_slot_.set(
      [&](Event::Dispatcher&) { return std::make_shared<ThreadLocalClusterInfo>(*this); });
}

LoadClusterEntryHandlePtr ProxyFilterConfig::addDynamicCluster(
    Extensions::Common::DynamicForwardProxy::DfpClusterSharedPtr cluster,
    const std::string& cluster_name, const std::string& host, const int port,
    LoadClusterEntryCallbacks& callbacks) {
  std::pair<bool, absl::optional<envoy::config::cluster::v3::Cluster>> sub_cluster_pair =
      cluster->createSubClusterConfig(cluster_name, host, port);

  if (!sub_cluster_pair.first) {
    ENVOY_LOG(debug, "cluster='{}' create failed due to max sub cluster limitation", cluster_name);
    return nullptr;
  }

  if (sub_cluster_pair.second.has_value()) {
    auto cluster = sub_cluster_pair.second.value();
    // TODO: a meaningful version_info.
    std::string version_info = "";
    ENVOY_LOG(debug, "deliver dynamic cluster {} creation to main thread", cluster_name);
    main_thread_dispatcher_.post([this, cluster, version_info]() {
      ENVOY_LOG(debug, "initializing dynamic cluster {} creation in main thread", cluster.name());

      // Set avoid_cds_removal to true to prevent the cluster from being removed during a CDS
      // update. As this cluster lifecycle is managed by DFP cluster, it should not be removed by
      // CDS. https://github.com/envoyproxy/envoy/issues/35171
      cluster_manager_.addOrUpdateCluster(
          cluster, version_info,
          Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.avoid_dfp_cluster_removal_on_cds_update"));
    });
  } else {
    ENVOY_LOG(debug, "cluster='{}' already created, waiting it warming", cluster_name);
  }

  // register a callback that will continue the request when the created cluster is ready.
  ENVOY_LOG(debug, "adding pending cluster for: {}", cluster_name);
  ThreadLocalClusterInfo& tls_cluster_info = *tls_slot_;
  return std::make_unique<LoadClusterEntryHandleImpl>(tls_cluster_info.pending_clusters_,
                                                      cluster_name, callbacks);
}

ProxyFilterConfig::ThreadLocalClusterInfo::~ThreadLocalClusterInfo() {
  for (const auto& it : pending_clusters_) {
    for (auto cluster : it.second) {
      cluster->cancel();
    }
  }
}
void ProxyFilterConfig::ThreadLocalClusterInfo::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand&) {
  ENVOY_LOG(debug, "thread local cluster {} added or updated", cluster_name);
  auto it = pending_clusters_.find(cluster_name);
  if (it != pending_clusters_.end()) {
    for (auto* cluster : it->second) {
      auto& callbacks = cluster->callbacks_;
      cluster->cancel();
      callbacks.onLoadClusterComplete();
    }
    pending_clusters_.erase(it);
  } else {
    ENVOY_LOG(debug, "but not pending request waiting on {}", cluster_name);
  }
}

void ProxyFilterConfig::ThreadLocalClusterInfo::onClusterRemoval(const std::string&) {
  // do nothing, should have no pending clusters.
}

ProxyPerRouteConfig::ProxyPerRouteConfig(
    const envoy::extensions::filters::http::dynamic_forward_proxy::v3::PerRouteConfig& config)
    : host_rewrite_(config.host_rewrite_literal()),
      host_rewrite_header_(Http::LowerCaseString(config.host_rewrite_header())) {}

void ProxyFilter::onDestroy() {
  // Make sure we destroy any active cache/cluster load handle in case we are getting reset and
  // deferred deleted.
  cache_load_handle_.reset();
  circuit_breaker_.reset();
  cluster_load_handle_.reset();
  if (cluster_init_timer_) {
    cluster_init_timer_->disableTimer();
    cluster_init_timer_.reset();
  }
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
  OptRef<const CustomClusterType> cluster_type = cluster_info_->clusterType();
  if (!cluster_type.has_value()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (cluster_type->name() != "envoy.clusters.dynamic_forward_proxy") {
    ENVOY_STREAM_LOG(debug, "cluster_type->name(): {} ", *this->decoder_callbacks_,
                     cluster_type->name());
    return Http::FilterHeadersStatus::Continue;
  }

  uint16_t default_port = 80;
  if (cluster_info_->transportSocketMatcher()
          .resolve(nullptr, nullptr)
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

  Extensions::Common::DynamicForwardProxy::DfpClusterSharedPtr dfp_cluster =
      config_->clusterStore()->load(cluster_info_->name());
  if (!dfp_cluster) {
    // This could happen in a very small race when users remove the DFP cluster and a route still
    // using it, which is not a good usage, will end with ServiceUnavailable.
    // Thread local cluster is existing due to the thread local cache, and the main thread notify
    // work thread is on the way.
    ENVOY_STREAM_LOG(debug, "dynamic forward cluster is gone", *this->decoder_callbacks_);
    this->decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable,
                                             ResponseStrings::get().DFPClusterIsGone, nullptr,
                                             absl::nullopt, RcDetails::get().DFPClusterIsGone);
    return Http::FilterHeadersStatus::StopIteration;
  }

  if (dfp_cluster->enableSubCluster()) {
    return loadDynamicCluster(dfp_cluster, headers, default_port);
  }

  circuit_breaker_ = config_->cache().canCreateDnsRequest();

  if (circuit_breaker_ == nullptr) {
    ENVOY_STREAM_LOG(debug, "pending request overflow", *this->decoder_callbacks_);
    this->decoder_callbacks_->sendLocalReply(
        Http::Code::ServiceUnavailable, ResponseStrings::get().PendingRequestOverflow, nullptr,
        absl::nullopt, RcDetails::get().PendingRequestOverflow);
    return Http::FilterHeadersStatus::StopIteration;
  }

  bool force_cache_refresh = false;
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.reresolve_if_no_connections")) {
    // For Envoy Mobile, we need to handle endpoints becoming unreachable and needing a new DNS
    // resolution on network change (WIFI to cellular and vice versa). This is expected to be
    // more performant than the current pattern when enable_drain_post_dns_refresh_ = true
    // in mobile/library/common/network/connectivity_manager.cc because that force-drains endpoints
    // even if addresses don't change where this does induce DNS latency if there hasn't been a
    // network change if the endpoint hasn't been referenced recently, but also guarantees there
    // will be a DNS resolution relevant to the current network and is more consistent with other
    // vetted and tested client stacks and resolution any time an endpoint becomes unreachable.
    //
    // If this runtime guard proves useful for Envoy Mobile, it will be replaced
    // either with a permanent knob or non-reloadable runtime guard (see TODO in
    // runtime_features.cc)
    auto dfp_lb =
        dynamic_cast<Extensions::Common::DynamicForwardProxy::DfpLb*>(&cluster->loadBalancer());
    if (dfp_lb) {
      std::string hostname = Common::DynamicForwardProxy::DnsHostInfo::normalizeHostForDfp(
          headers.getHostValue(), default_port);
      auto host = dfp_lb->findHostByName(hostname);
      if (host && !host->used()) {
        force_cache_refresh = true;
      }
    }
  }

  latchTime(decoder_callbacks_, DNS_START);
  const bool is_proxying = isProxying();
  auto result = config_->cache().loadDnsCacheEntryWithForceRefresh(
      headers.Host()->value().getStringView(), default_port, is_proxying, force_cache_refresh,
      *this);
  cache_load_handle_ = std::move(result.handle_);
  if (cache_load_handle_ == nullptr) {
    circuit_breaker_.reset();
  }

  switch (result.status_) {
  case LoadDnsCacheEntryStatus::InCache: {
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_STREAM_LOG(debug, "DNS cache entry already loaded, continuing", *decoder_callbacks_);

    auto const& host = result.host_info_;
    latchTime(decoder_callbacks_, DNS_END);
    if (is_proxying) {
      ENVOY_BUG(host.has_value(), "Proxying request but no host entry in DNS cache.");
      return Http::FilterHeadersStatus::Continue;
    }
    if (!host.has_value() || !host.value()->address()) {
      onDnsResolutionFail((host.has_value() && *host) ? ((*host)->details()) : "no_host");
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

Http::FilterHeadersStatus ProxyFilter::loadDynamicCluster(
    Extensions::Common::DynamicForwardProxy::DfpClusterSharedPtr cluster,
    Http::RequestHeaderMap& headers, uint16_t default_port) {
  const auto host_attributes = Http::Utility::parseAuthority(headers.getHostValue());
  auto host = std::string(host_attributes.host_);
  auto port = host_attributes.port_.value_or(default_port);

  latchTime(decoder_callbacks_, DNS_START);

  // cluster name is prefix + host + port
  auto cluster_name = "DFPCluster:" + host + ":" + std::to_string(port);
  Upstream::ThreadLocalCluster* local_cluster =
      config_->clusterManager().getThreadLocalCluster(cluster_name);
  if (local_cluster && cluster->touch(cluster_name)) {
    ENVOY_STREAM_LOG(debug, "using the thread local cluster after touch success",
                     *decoder_callbacks_);
    latchTime(decoder_callbacks_, DNS_END);
    return Http::FilterHeadersStatus::Continue;
  }

  // Still need to add dynamic cluster again even the thread local cluster exists while touch
  // failed, that means the cluster is removed in main thread due to ttl reached.
  // Otherwise, we may not be able to get the thread local cluster in router.

  // Create a new cluster & register a callback to tls
  cluster_load_handle_ = config_->addDynamicCluster(cluster, cluster_name, host, port, *this);
  if (!cluster_load_handle_) {
    ENVOY_STREAM_LOG(debug, "sub clusters overflow", *this->decoder_callbacks_);
    this->decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable,
                                             ResponseStrings::get().SubClusterOverflow, nullptr,
                                             absl::nullopt, RcDetails::get().SubClusterOverflow);
    return Http::FilterHeadersStatus::StopIteration;
  }

  cluster_init_timer_ =
      decoder_callbacks_->dispatcher().createTimer([this]() { onClusterInitTimeout(); });
  cluster_init_timer_->enableTimer(config_->clusterInitTimeout());

  ENVOY_STREAM_LOG(debug, "waiting to load cluster entry", *decoder_callbacks_);
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
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

void ProxyFilter::onLoadClusterComplete() {
  ASSERT(cluster_init_timer_);
  cluster_init_timer_->disableTimer();
  cluster_init_timer_.reset();

  latchTime(decoder_callbacks_, DNS_END);
  ENVOY_STREAM_LOG(debug, "load cluster complete, continuing", *decoder_callbacks_);
  decoder_callbacks_->continueDecoding();
}

void ProxyFilter::onClusterInitTimeout() {
  latchTime(decoder_callbacks_, DNS_END);
  ENVOY_STREAM_LOG(debug, "load cluster failed, aborting", *decoder_callbacks_);
  cluster_load_handle_.reset();
  decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable,
                                     ResponseStrings::get().SubClusterWarmingTimeout, nullptr,
                                     absl::nullopt, RcDetails::get().SubClusterWarmingTimeout);
}

void ProxyFilter::onDnsResolutionFail(absl::string_view details) {
  if (isProxying()) {
    decoder_callbacks_->continueDecoding();
    return;
  }

  decoder_callbacks_->streamInfo().setResponseFlag(
      StreamInfo::CoreResponseFlag::DnsResolutionFailed);
  std::string details_str = "";
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.dns_details")) {
    details_str = StringUtil::replaceAllEmptySpace(details);
    ASSERT(details_str != "not_resolved");
  }
  decoder_callbacks_->sendLocalReply(
      Http::Code::ServiceUnavailable, ResponseStrings::get().DnsResolutionFailure, nullptr,
      absl::nullopt, absl::StrCat(RcDetails::get().DnsResolutionFailure, "{", details_str, "}"));
}

void ProxyFilter::onLoadDnsCacheComplete(
    const Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {
  ENVOY_STREAM_LOG(debug, "load DNS cache complete, continuing after adding resolved host: {}",
                   *decoder_callbacks_, host_info->resolvedHost());
  latchTime(decoder_callbacks_, DNS_END);
  ASSERT(circuit_breaker_ != nullptr);
  circuit_breaker_.reset();

  if (!host_info->address()) {
    onDnsResolutionFail(host_info->details());
    return;
  }
  addHostAddressToFilterState(host_info->address());

  decoder_callbacks_->continueDecoding();
}

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
