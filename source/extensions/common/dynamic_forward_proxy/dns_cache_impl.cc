#include "extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "common/config/utility.h"
#include "common/http/utility.h"
#include "common/network/utility.h"

// TODO(mattklein123): Move DNS family helpers to a smaller include.
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

DnsCacheImpl::DnsCacheImpl(
    Event::Dispatcher& main_thread_dispatcher, ThreadLocal::SlotAllocator& tls,
    Random::RandomGenerator& random, Runtime::Loader& loader, Stats::Scope& root_scope,
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config)
    : main_thread_dispatcher_(main_thread_dispatcher),
      dns_lookup_family_(Upstream::getDnsLookupFamilyFromEnum(config.dns_lookup_family())),
      resolver_(main_thread_dispatcher.createDnsResolver({}, config.use_tcp_for_dns_lookups())),
      tls_slot_(tls.allocateSlot()),
      scope_(root_scope.createScope(fmt::format("dns_cache.{}.", config.name()))),
      stats_(generateDnsCacheStats(*scope_)),
      resource_manager_(*scope_, loader, config.name(), config.dns_cache_circuit_breaker()),
      refresh_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, dns_refresh_rate, 60000)),
      failure_backoff_strategy_(
          Config::Utility::prepareDnsRefreshStrategy<
              envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig>(
              config, refresh_interval_.count(), random)),
      host_ttl_(PROTOBUF_GET_MS_OR_DEFAULT(config, host_ttl, 300000)),
      max_hosts_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_hosts, 1024)) {
  tls_slot_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalHostInfo>(); });
  updateTlsHostsMap();
}

DnsCacheImpl::~DnsCacheImpl() {
  for (const auto& primary_host : primary_hosts_) {
    if (primary_host.second->active_query_ != nullptr) {
      primary_host.second->active_query_->cancel();
    }
  }

  for (auto update_callbacks : update_callbacks_) {
    update_callbacks->cancel();
  }
}

DnsCacheStats DnsCacheImpl::generateDnsCacheStats(Stats::Scope& scope) {
  return {ALL_DNS_CACHE_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

DnsCacheImpl::LoadDnsCacheEntryResult
DnsCacheImpl::loadDnsCacheEntry(absl::string_view host, uint16_t default_port,
                                LoadDnsCacheEntryCallbacks& callbacks) {
  ENVOY_LOG(debug, "thread local lookup for host '{}'", host);
  auto& tls_host_info = tls_slot_->getTyped<ThreadLocalHostInfo>();
  auto tls_host = tls_host_info.host_map_->find(host);
  if (tls_host != tls_host_info.host_map_->end()) {
    ENVOY_LOG(debug, "thread local hit for host '{}'", host);
    return {LoadDnsCacheEntryStatus::InCache, nullptr};
  } else if (tls_host_info.host_map_->size() >= max_hosts_) {
    // Given that we do this check in thread local context, it's possible for two threads to race
    // and potentially go slightly above the configured max hosts. This is an OK given compromise
    // given how much simpler the implementation is.
    ENVOY_LOG(debug, "DNS cache overflow for host '{}'", host);
    stats_.host_overflow_.inc();
    return {LoadDnsCacheEntryStatus::Overflow, nullptr};
  } else {
    ENVOY_LOG(debug, "thread local miss for host '{}', posting to main thread", host);
    main_thread_dispatcher_.post(
        [this, host = std::string(host), default_port]() { startCacheLoad(host, default_port); });
    return {LoadDnsCacheEntryStatus::Loading,
            std::make_unique<LoadDnsCacheEntryHandleImpl>(tls_host_info.pending_resolutions_, host,
                                                          callbacks)};
  }
}

Upstream::ResourceAutoIncDecPtr
DnsCacheImpl::canCreateDnsRequest(ResourceLimitOptRef pending_requests) {
  const auto has_pending_requests = pending_requests.has_value();
  auto& current_pending_requests =
      has_pending_requests ? pending_requests->get() : resource_manager_.pendingRequests();
  if (!current_pending_requests.canCreate()) {
    if (!has_pending_requests) {
      stats_.dns_rq_pending_overflow_.inc();
    }
    return nullptr;
  }
  return std::make_unique<Upstream::ResourceAutoIncDec>(current_pending_requests);
}

absl::flat_hash_map<std::string, DnsHostInfoSharedPtr> DnsCacheImpl::hosts() {
  absl::flat_hash_map<std::string, DnsHostInfoSharedPtr> ret;
  for (const auto& host : primary_hosts_) {
    // Only include hosts that have ever resolved to an address.
    if (host.second->host_info_->address_ != nullptr) {
      ret.emplace(host.first, host.second->host_info_);
    }
  }
  return ret;
}

absl::optional<const DnsHostInfoSharedPtr> DnsCacheImpl::getHost(absl::string_view host_name) {
  // Find a host with the given name.
  auto it = primary_hosts_.find(host_name);
  if (it == primary_hosts_.end()) {
    return {};
  }

  // Extract host info.
  auto&& host_info = it->second->host_info_;

  // Only include hosts that have ever resolved to an address.
  if (host_info->address_ == nullptr) {
    return {};
  }

  // Return host info.
  return host_info;
}

DnsCacheImpl::AddUpdateCallbacksHandlePtr
DnsCacheImpl::addUpdateCallbacks(UpdateCallbacks& callbacks) {
  return std::make_unique<AddUpdateCallbacksHandleImpl>(update_callbacks_, callbacks);
}

void DnsCacheImpl::startCacheLoad(const std::string& host, uint16_t default_port) {
  // It's possible for multiple requests to race trying to start a resolution. If a host is
  // already in the map it's either in the process of being resolved or the resolution is already
  // heading out to the worker threads. Either way the pending resolution will be completed.
  const auto primary_host_it = primary_hosts_.find(host);
  if (primary_host_it != primary_hosts_.end()) {
    ENVOY_LOG(debug, "main thread resolve for host '{}' skipped. Entry present", host);
    return;
  }

  const auto host_attributes = Http::Utility::parseAuthority(host);

  // TODO(mattklein123): Right now, the same host with different ports will become two
  // independent primary hosts with independent DNS resolutions. I'm not sure how much this will
  // matter, but we could consider collapsing these down and sharing the underlying DNS resolution.
  auto& primary_host = *primary_hosts_
                            // try_emplace() is used here for direct argument forwarding.
                            .try_emplace(host, std::make_unique<PrimaryHostInfo>(
                                                   *this, std::string(host_attributes.host_),
                                                   host_attributes.port_.value_or(default_port),
                                                   host_attributes.is_ip_address_,
                                                   [this, host]() { onReResolve(host); }))
                            .first->second;
  startResolve(host, primary_host);
}

void DnsCacheImpl::onReResolve(const std::string& host) {
  const auto primary_host_it = primary_hosts_.find(host);
  ASSERT(primary_host_it != primary_hosts_.end());

  const std::chrono::steady_clock::duration now_duration =
      main_thread_dispatcher_.timeSource().monotonicTime().time_since_epoch();
  ENVOY_LOG(debug, "host='{}' TTL check: now={} last_used={}", primary_host_it->first,
            now_duration.count(),
            primary_host_it->second->host_info_->last_used_time_.load().count());
  if (now_duration - primary_host_it->second->host_info_->last_used_time_.load() > host_ttl_) {
    ENVOY_LOG(debug, "host='{}' TTL expired, removing", host);
    // If the host has no address then that means that the DnsCacheImpl has never
    // runAddUpdateCallbacks for this host, and thus the callback targets are not aware of it.
    // Therefore, runRemoveCallbacks should only be ran if the host's address != nullptr.
    if (primary_host_it->second->host_info_->address_) {
      runRemoveCallbacks(host);
    }
    primary_hosts_.erase(primary_host_it);
    updateTlsHostsMap();
  } else {
    startResolve(host, *primary_host_it->second);
  }
}

void DnsCacheImpl::startResolve(const std::string& host, PrimaryHostInfo& host_info) {
  ENVOY_LOG(debug, "starting main thread resolve for host='{}' dns='{}' port='{}'", host,
            host_info.host_info_->resolved_host_, host_info.port_);
  ASSERT(host_info.active_query_ == nullptr);

  stats_.dns_query_attempt_.inc();
  host_info.active_query_ =
      resolver_->resolve(host_info.host_info_->resolved_host_, dns_lookup_family_,
                         [this, host](Network::DnsResolver::ResolutionStatus status,
                                      std::list<Network::DnsResponse>&& response) {
                           finishResolve(host, status, std::move(response));
                         });
}

void DnsCacheImpl::finishResolve(const std::string& host,
                                 Network::DnsResolver::ResolutionStatus status,
                                 std::list<Network::DnsResponse>&& response) {
  ENVOY_LOG(debug, "main thread resolve complete for host '{}'. {} results", host, response.size());
  const auto primary_host_it = primary_hosts_.find(host);
  ASSERT(primary_host_it != primary_hosts_.end());

  auto& primary_host_info = *primary_host_it->second;
  primary_host_info.active_query_ = nullptr;
  const bool first_resolve = !primary_host_info.host_info_->first_resolve_complete_;
  primary_host_info.host_info_->first_resolve_complete_ = true;

  // If the DNS resolver successfully resolved with an empty response list, the dns cache does not
  // update. This ensures that a potentially previously resolved address does not stabilize back to
  // 0 hosts.
  const auto new_address = !response.empty()
                               ? Network::Utility::getAddressWithPort(*(response.front().address_),
                                                                      primary_host_info.port_)
                               : nullptr;

  if (status == Network::DnsResolver::ResolutionStatus::Failure) {
    stats_.dns_query_failure_.inc();
  } else {
    stats_.dns_query_success_.inc();
  }

  // Only the change the address if:
  // 1) The new address is valid &&
  // 2a) The host doesn't yet have an address ||
  // 2b) The host has a changed address.
  //
  // This means that once a host gets an address it will stick even in the case of a subsequent
  // resolution failure.
  bool address_changed = false;
  if (new_address != nullptr && (primary_host_info.host_info_->address_ == nullptr ||
                                 *primary_host_info.host_info_->address_ != *new_address)) {
    ENVOY_LOG(debug, "host '{}' address has changed", host);
    primary_host_info.host_info_->address_ = new_address;
    runAddUpdateCallbacks(host, primary_host_info.host_info_);
    address_changed = true;
    stats_.host_address_changed_.inc();
  }

  if (first_resolve || address_changed) {
    updateTlsHostsMap();
  }

  // Kick off the refresh timer.
  // TODO(mattklein123): Consider jitter here. It may not be necessary since the initial host
  // is populated dynamically.
  if (status == Network::DnsResolver::ResolutionStatus::Success) {
    failure_backoff_strategy_->reset();
    primary_host_info.refresh_timer_->enableTimer(refresh_interval_);
    ENVOY_LOG(debug, "DNS refresh rate reset for host '{}', refresh rate {} ms", host,
              refresh_interval_.count());
  } else {
    const uint64_t refresh_interval = failure_backoff_strategy_->nextBackOffMs();
    primary_host_info.refresh_timer_->enableTimer(std::chrono::milliseconds(refresh_interval));
    ENVOY_LOG(debug, "DNS refresh rate reset for host '{}', (failure) refresh rate {} ms", host,
              refresh_interval);
  }
}

void DnsCacheImpl::runAddUpdateCallbacks(const std::string& host,
                                         const DnsHostInfoSharedPtr& host_info) {
  for (auto callbacks : update_callbacks_) {
    callbacks->callbacks_.onDnsHostAddOrUpdate(host, host_info);
  }
}

void DnsCacheImpl::runRemoveCallbacks(const std::string& host) {
  for (auto callbacks : update_callbacks_) {
    callbacks->callbacks_.onDnsHostRemove(host);
  }
}

void DnsCacheImpl::updateTlsHostsMap() {
  TlsHostMapSharedPtr new_host_map = std::make_shared<TlsHostMap>();
  for (const auto& primary_host : primary_hosts_) {
    // Do not include hosts that have not resolved at least once.
    if (primary_host.second->host_info_->first_resolve_complete_) {
      new_host_map->emplace(primary_host.first, primary_host.second->host_info_);
    }
  }

  tls_slot_->runOnAllThreads([new_host_map](ThreadLocal::ThreadLocalObjectSharedPtr object)
                                 -> ThreadLocal::ThreadLocalObjectSharedPtr {
    object->asType<ThreadLocalHostInfo>().updateHostMap(new_host_map);
    return object;
  });
}

DnsCacheImpl::ThreadLocalHostInfo::~ThreadLocalHostInfo() {
  // Make sure we cancel any handles that still exist.
  for (auto pending_resolution : pending_resolutions_) {
    pending_resolution->cancel();
  }
}

void DnsCacheImpl::ThreadLocalHostInfo::updateHostMap(const TlsHostMapSharedPtr& new_host_map) {
  host_map_ = new_host_map;
  for (auto pending_resolution_it = pending_resolutions_.begin();
       pending_resolution_it != pending_resolutions_.end();) {
    auto& pending_resolution = **pending_resolution_it;
    if (host_map_->count(pending_resolution.host_) != 0) {
      auto& callbacks = pending_resolution.callbacks_;
      pending_resolution.cancel();
      pending_resolution_it = pending_resolutions_.erase(pending_resolution_it);
      callbacks.onLoadDnsCacheComplete();
    } else {
      ++pending_resolution_it;
    }
  }
}

DnsCacheImpl::PrimaryHostInfo::PrimaryHostInfo(DnsCacheImpl& parent,
                                               absl::string_view host_to_resolve, uint16_t port,
                                               bool is_ip_address, const Event::TimerCb& timer_cb)
    : parent_(parent), port_(port),
      refresh_timer_(parent.main_thread_dispatcher_.createTimer(timer_cb)),
      host_info_(std::make_shared<DnsHostInfoImpl>(parent.main_thread_dispatcher_.timeSource(),
                                                   host_to_resolve, is_ip_address)) {
  parent_.stats_.host_added_.inc();
  parent_.stats_.num_hosts_.inc();
}

DnsCacheImpl::PrimaryHostInfo::~PrimaryHostInfo() {
  parent_.stats_.host_removed_.inc();
  parent_.stats_.num_hosts_.dec();
}

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
