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
    Server::Configuration::FactoryContext& context)
    : dns_cache_manager_(cache_manager_factory.get()),
      dns_cache_(dns_cache_manager_->getCache(proto_config.dns_cache_config())),
      cluster_manager_(context.clusterManager()),
      main_thread_dispatcher_(context.mainThreadDispatcher()), tls_slot_(context.threadLocal()),
      save_upstream_address_(proto_config.save_upstream_address()),
      enable_strict_dns_cluster_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.enable_strict_dns_sub_cluster_for_dfp_cluster")) {
  tls_slot_.set(
      [&](Event::Dispatcher&) { return std::make_shared<ThreadLocalClusterInfo>(*this); });
}

LoadClusterEntryHandlePtr
ProxyFilterConfig::addDynamicCluster(Upstream::ClusterInfoConstSharedPtr parent_info,
                                     const std::string& cluster_name, const std::string& host,
                                     const int port, LoadClusterEntryCallbacks& callbacks) {
  // clone cluster config from the parent DFP cluster.
  envoy::config::cluster::v3::Cluster config = parent_info->originalClusterConfig();

  // inherit dns config from cluster_type
  auto cluster_type = config.cluster_type();
  envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig proto_config;
  MessageUtil::unpackTo(cluster_type.typed_config(), proto_config);
  config.set_dns_lookup_family(proto_config.dns_cache_config().dns_lookup_family());

  // overwrite to a strict_dns cluster.
  config.set_name(cluster_name);
  config.clear_cluster_type();
  config.set_type(
      envoy::config::cluster::v3::Cluster_DiscoveryType::Cluster_DiscoveryType_STRICT_DNS);
  config.set_lb_policy(envoy::config::cluster::v3::Cluster_LbPolicy::Cluster_LbPolicy_ROUND_ROBIN);

  /*
    config.set_dns_lookup_family(
        envoy::config::cluster::v3::Cluster_DnsLookupFamily::Cluster_DnsLookupFamily_V4_ONLY);
    config.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(parent_info->connectTimeout().count()));
  */

  auto load_assignments = config.mutable_load_assignment();
  load_assignments->set_cluster_name(cluster_name);
  load_assignments->clear_endpoints();

  auto socket_address = load_assignments->add_endpoints()
                            ->add_lb_endpoints()
                            ->mutable_endpoint()
                            ->mutable_address()
                            ->mutable_socket_address();
  socket_address->set_address(host);
  socket_address->set_port_value(port);

  std::string version_info = "";

  ENVOY_LOG(debug, "deliver dynamic cluster {} creation to main thread", cluster_name);
  main_thread_dispatcher_.post([this, cluster = config, version_info]() {
    ENVOY_LOG(debug, "initilizing dynamic cluster {} creation in main thread", cluster.name());
    cluster_manager_.addOrUpdateCluster(cluster, version_info);
  });

  // register a callback that will continue the request when the created cluster is ready.
  ENVOY_LOG(debug, "adding pending cluster for: {}", cluster_name);
  ThreadLocalClusterInfo& tls_cluster_info = *tls_slot_;
  return std::make_unique<LoadClusterEntryHandleImpl>(tls_cluster_info.pending_clusters_,
                                                      cluster_name, callbacks);
}

Upstream::ClusterUpdateCallbacksHandlePtr
ProxyFilterConfig::addThreadLocalClusterUpdateCallbacks() {
  return cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);
}

ProxyFilterConfig::ThreadLocalClusterInfo::~ThreadLocalClusterInfo() {
  for (const auto& it : pending_clusters_) {
    for (auto cluster : it.second) {
      cluster->cancel();
    }
  }
}

void ProxyFilterConfig::onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) {
  const std::string& cluster_name = cluster.info()->name();
  ENVOY_LOG(debug, "thread local cluster {} added or updated", cluster_name);
  ThreadLocalClusterInfo& tls_cluster_info = *tls_slot_;
  auto it = tls_cluster_info.pending_clusters_.find(cluster_name);
  if (it != tls_cluster_info.pending_clusters_.end()) {
    for (auto* cluster : it->second) {
      auto& callbacks = cluster->callbacks_;
      cluster->cancel();
      callbacks.onLoadClusterComplete();
    }
    tls_cluster_info.pending_clusters_.erase(it);
  } else {
    ENVOY_LOG(debug, "but not pending request waiting on {}", cluster_name);
  }
}

void ProxyFilterConfig::onClusterRemoval(const std::string&) {
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

  if (config_->enableStrictDnsCluster()) {
    return loadDynamicCluster(headers, default_port);
  }

  circuit_breaker_ = config_->cache().canCreateDnsRequest();

  if (circuit_breaker_ == nullptr) {
    ENVOY_STREAM_LOG(debug, "pending request overflow", *this->decoder_callbacks_);
    this->decoder_callbacks_->sendLocalReply(
        Http::Code::ServiceUnavailable, ResponseStrings::get().PendingRequestOverflow, nullptr,
        absl::nullopt, RcDetails::get().PendingRequestOverflow);
    return Http::FilterHeadersStatus::StopIteration;
  }

  latchTime(decoder_callbacks_, DNS_START);
  // See the comments in dns_cache.h for how loadDnsCacheEntry() handles hosts with embedded
  // ports.
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

Http::FilterHeadersStatus ProxyFilter::loadDynamicCluster(Http::RequestHeaderMap& headers,
                                                          uint16_t default_port) {
  const auto host_attributes = Http::Utility::parseAuthority(headers.getHostValue());
  auto host = std::string(host_attributes.host_);
  auto port = host_attributes.port_.value_or(default_port);
  // TODO: another cluster type when it's an IP.
  // host_attributes.is_ip_address_;

  latchTime(decoder_callbacks_, DNS_START);

  // cluster name is prefix + host + port
  auto cluster_name = "DFPCluster:" + host + ":" + std::to_string(port);
  Upstream::ThreadLocalCluster* dfp_cluster =
      config_->clusterManager().getThreadLocalCluster(cluster_name);
  if (dfp_cluster) {
    // DFP cluster already exists
    latchTime(decoder_callbacks_, DNS_END);
    return Http::FilterHeadersStatus::Continue;
  }

  // not found, create a new cluster & register a callback to tls
  cluster_load_handle_ = config_->addDynamicCluster(cluster_info_, cluster_name, host, port, *this);
  ASSERT(cluster_load_handle_ != nullptr);
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
  latchTime(decoder_callbacks_, DNS_END);
  ENVOY_STREAM_LOG(debug, "load cluster complete, continuing", *decoder_callbacks_);
  decoder_callbacks_->continueDecoding();
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
