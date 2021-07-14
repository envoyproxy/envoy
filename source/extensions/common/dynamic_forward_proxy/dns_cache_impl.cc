#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/utility.h"

// TODO(mattklein123): Move DNS family helpers to a smaller include.
#include "source/common/upstream/upstream_impl.h"

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
      resolver_(selectDnsResolver(config, main_thread_dispatcher)), tls_slot_(tls),
      scope_(root_scope.createScope(fmt::format("dns_cache.{}.", config.name()))),
      stats_(generateDnsCacheStats(*scope_)),
      resource_manager_(*scope_, loader, config.name(), config.dns_cache_circuit_breaker()),
      refresh_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, dns_refresh_rate, 60000)),
      timeout_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, dns_query_timeout, 5000)),
      failure_backoff_strategy_(
          Config::Utility::prepareDnsRefreshStrategy<
              envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig>(
              config, refresh_interval_.count(), random)),
      host_ttl_(PROTOBUF_GET_MS_OR_DEFAULT(config, host_ttl, 300000)),
      max_hosts_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_hosts, 1024)) {
  tls_slot_.set([&](Event::Dispatcher&) { return std::make_shared<ThreadLocalHostInfo>(*this); });

  if (static_cast<size_t>(config.preresolve_hostnames().size()) > max_hosts_) {
    throw EnvoyException(fmt::format(
        "DNS Cache [{}] configured with preresolve_hostnames={} larger than max_hosts={}",
        config.name(), config.preresolve_hostnames().size(), max_hosts_));
  }

  // Preresolved hostnames are resolved without a read lock on primary hosts because it is done
  // during object construction.
  for (const auto& hostname : config.preresolve_hostnames()) {
    // No need to get a resolution handle on this resolution as the only outcome needed is for the
    // cache to load an entry. Further if this particular resolution fails all the is lost is the
    // potential optimization of having the entry be preresolved the first time a true consumer of
    // this DNS cache asks for it.
    main_thread_dispatcher_.post(
        [this, host = hostname.address(), default_port = hostname.port_value()]() {
          startCacheLoad(host, default_port);
        });
  }
}

DnsCacheImpl::~DnsCacheImpl() {
  for (const auto& primary_host : primary_hosts_) {
    if (primary_host.second->active_query_ != nullptr) {
      primary_host.second->active_query_->cancel(
          Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
    }
  }

  for (auto update_callbacks : update_callbacks_) {
    update_callbacks->cancel();
  }
}

Network::DnsResolverSharedPtr DnsCacheImpl::selectDnsResolver(
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config,
    Event::Dispatcher& main_thread_dispatcher) {
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options;
  std::vector<Network::Address::InstanceConstSharedPtr> resolvers;
  if (config.has_dns_resolution_config()) {
    dns_resolver_options.CopyFrom(config.dns_resolution_config().dns_resolver_options());
    if (!config.dns_resolution_config().resolvers().empty()) {
      const auto& resolver_addrs = config.dns_resolution_config().resolvers();
      resolvers.reserve(resolver_addrs.size());
      for (const auto& resolver_addr : resolver_addrs) {
        resolvers.push_back(Network::Address::resolveProtoAddress(resolver_addr));
      }
    }
  } else {
    // Field bool `use_tcp_for_dns_lookups` will be deprecated in future. To be backward
    // compatible utilize config.use_tcp_for_dns_lookups() if `config.dns_resolution_config`
    // is not set.
    dns_resolver_options.set_use_tcp_for_dns_lookups(config.use_tcp_for_dns_lookups());
  }
  return main_thread_dispatcher.createDnsResolver(resolvers, dns_resolver_options);
}

DnsCacheStats DnsCacheImpl::generateDnsCacheStats(Stats::Scope& scope) {
  return {ALL_DNS_CACHE_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

DnsCacheImpl::LoadDnsCacheEntryResult
DnsCacheImpl::loadDnsCacheEntry(absl::string_view host, uint16_t default_port,
                                LoadDnsCacheEntryCallbacks& callbacks) {
  ENVOY_LOG(debug, "thread local lookup for host '{}'", host);
  ThreadLocalHostInfo& tls_host_info = *tls_slot_;

  auto [is_overflow, host_info] = [&]() {
    absl::ReaderMutexLock read_lock{&primary_hosts_lock_};
    auto tls_host = primary_hosts_.find(host);
    return std::make_tuple(
        primary_hosts_.size() >= max_hosts_,
        (tls_host != primary_hosts_.end() && tls_host->second->host_info_->firstResolveComplete())
            ? absl::optional<DnsHostInfoSharedPtr>(tls_host->second->host_info_)
            : absl::nullopt);
  }();

  if (host_info) {
    ENVOY_LOG(debug, "cache hit for host '{}'", host);
    return {LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
  } else if (is_overflow) {
    ENVOY_LOG(debug, "DNS cache overflow for host '{}'", host);
    stats_.host_overflow_.inc();
    return {LoadDnsCacheEntryStatus::Overflow, nullptr, absl::nullopt};
  } else {
    ENVOY_LOG(debug, "cache miss for host '{}', posting to main thread", host);
    main_thread_dispatcher_.post(
        [this, host = std::string(host), default_port]() { startCacheLoad(host, default_port); });
    return {LoadDnsCacheEntryStatus::Loading,
            std::make_unique<LoadDnsCacheEntryHandleImpl>(tls_host_info.pending_resolutions_, host,
                                                          callbacks),
            absl::nullopt};
  }
}

Upstream::ResourceAutoIncDecPtr DnsCacheImpl::canCreateDnsRequest() {
  auto& current_pending_requests = resource_manager_.pendingRequests();
  if (!current_pending_requests.canCreate()) {
    stats_.dns_rq_pending_overflow_.inc();
    return nullptr;
  }
  return std::make_unique<Upstream::ResourceAutoIncDec>(current_pending_requests);
}

void DnsCacheImpl::iterateHostMap(IterateHostMapCb iterate_callback) {
  absl::ReaderMutexLock reader_lock{&primary_hosts_lock_};
  for (const auto& host : primary_hosts_) {
    // Only include hosts that have ever resolved to an address.
    if (host.second->host_info_->address() != nullptr) {
      iterate_callback(host.first, host.second->host_info_);
    }
  }
}

absl::optional<const DnsHostInfoSharedPtr> DnsCacheImpl::getHost(absl::string_view host_name) {
  // Find a host with the given name.
  const auto host_info = [&]() -> const DnsHostInfoSharedPtr {
    absl::ReaderMutexLock reader_lock{&primary_hosts_lock_};
    auto it = primary_hosts_.find(host_name);
    return it != primary_hosts_.end() ? it->second->host_info_ : nullptr;
  }();

  // Only include hosts that have ever resolved to an address.
  if (!host_info || host_info->address() == nullptr) {
    return {};
  } else {
    return host_info;
  }
}

DnsCacheImpl::AddUpdateCallbacksHandlePtr
DnsCacheImpl::addUpdateCallbacks(UpdateCallbacks& callbacks) {
  return std::make_unique<AddUpdateCallbacksHandleImpl>(update_callbacks_, callbacks);
}

void DnsCacheImpl::startCacheLoad(const std::string& host, uint16_t default_port) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());

  // It's possible for multiple requests to race trying to start a resolution. If a host is
  // already in the map it's either in the process of being resolved or the resolution is already
  // heading out to the worker threads. Either way the pending resolution will be completed.

  // Functions like this one that modify primary_hosts_ are only called in the main thread so we
  // know it is safe to use the PrimaryHostInfo pointers outside of the lock.
  auto* primary_host = [&]() {
    absl::ReaderMutexLock reader_lock{&primary_hosts_lock_};
    auto host_it = primary_hosts_.find(host);
    return host_it != primary_hosts_.end() ? host_it->second.get() : nullptr;
  }();

  if (primary_host) {
    ENVOY_LOG(debug, "main thread resolve for host '{}' skipped. Entry present", host);
    return;
  }

  const auto host_attributes = Http::Utility::parseAuthority(host);

  // TODO(mattklein123): Right now, the same host with different ports will become two
  // independent primary hosts with independent DNS resolutions. I'm not sure how much this will
  // matter, but we could consider collapsing these down and sharing the underlying DNS resolution.
  {
    absl::WriterMutexLock writer_lock{&primary_hosts_lock_};
    primary_host = primary_hosts_
                       // try_emplace() is used here for direct argument forwarding.
                       .try_emplace(host, std::make_unique<PrimaryHostInfo>(
                                              *this, std::string(host_attributes.host_),
                                              host_attributes.port_.value_or(default_port),
                                              host_attributes.is_ip_address_,
                                              [this, host]() { onReResolve(host); },
                                              [this, host]() { onResolveTimeout(host); }))
                       .first->second.get();
  }

  startResolve(host, *primary_host);
}

DnsCacheImpl::PrimaryHostInfo& DnsCacheImpl::getPrimaryHost(const std::string& host) {
  // Functions modify primary_hosts_ are only called in the main thread so we
  // know it is safe to use the PrimaryHostInfo pointers outside of the lock.
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  absl::ReaderMutexLock reader_lock{&primary_hosts_lock_};
  const auto primary_host_it = primary_hosts_.find(host);
  ASSERT(primary_host_it != primary_hosts_.end());
  return *(primary_host_it->second.get());
}

void DnsCacheImpl::onResolveTimeout(const std::string& host) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());

  auto& primary_host = getPrimaryHost(host);
  ENVOY_LOG(debug, "host='{}' resolution timeout", host);
  stats_.dns_query_timeout_.inc();
  primary_host.active_query_->cancel(Network::ActiveDnsQuery::CancelReason::Timeout);
  finishResolve(host, Network::DnsResolver::ResolutionStatus::Failure, {});
}

void DnsCacheImpl::onReResolve(const std::string& host) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  // If we need to erase the host, hold onto the PrimaryHostInfo object that owns this callback.
  // This is defined at function scope so that it is only erased on function exit to avoid
  // use-after-free issues
  PrimaryHostInfoPtr host_to_erase;

  auto& primary_host = getPrimaryHost(host);
  const std::chrono::steady_clock::duration now_duration =
      main_thread_dispatcher_.timeSource().monotonicTime().time_since_epoch();
  auto last_used_time = primary_host.host_info_->lastUsedTime();
  ENVOY_LOG(debug, "host='{}' TTL check: now={} last_used={}", host, now_duration.count(),
            last_used_time.count());
  if ((now_duration - last_used_time) > host_ttl_) {
    ENVOY_LOG(debug, "host='{}' TTL expired, removing", host);
    // If the host has no address then that means that the DnsCacheImpl has never
    // runAddUpdateCallbacks for this host, and thus the callback targets are not aware of it.
    // Therefore, runRemoveCallbacks should only be ran if the host's address != nullptr.
    if (primary_host.host_info_->address()) {
      runRemoveCallbacks(host);
    }
    {
      absl::WriterMutexLock writer_lock{&primary_hosts_lock_};
      auto host_it = primary_hosts_.find(host);
      ASSERT(host_it != primary_hosts_.end());
      host_to_erase = std::move(host_it->second);
      primary_hosts_.erase(host_it);
    }
    notifyThreads(host, primary_host.host_info_);
  } else {
    startResolve(host, primary_host);
  }
}

void DnsCacheImpl::startResolve(const std::string& host, PrimaryHostInfo& host_info) {
  ENVOY_LOG(debug, "starting main thread resolve for host='{}' dns='{}' port='{}'", host,
            host_info.host_info_->resolvedHost(), host_info.port_);
  ASSERT(host_info.active_query_ == nullptr);

  stats_.dns_query_attempt_.inc();

  host_info.timeout_timer_->enableTimer(timeout_interval_, nullptr);
  host_info.active_query_ =
      resolver_->resolve(host_info.host_info_->resolvedHost(), dns_lookup_family_,
                         [this, host](Network::DnsResolver::ResolutionStatus status,
                                      std::list<Network::DnsResponse>&& response) {
                           finishResolve(host, status, std::move(response));
                         });
}

void DnsCacheImpl::finishResolve(const std::string& host,
                                 Network::DnsResolver::ResolutionStatus status,
                                 std::list<Network::DnsResponse>&& response) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  ENVOY_LOG(debug, "main thread resolve complete for host '{}'. {} results", host, response.size());

  // Functions like this one that modify primary_hosts_ are only called in the main thread so we
  // know it is safe to use the PrimaryHostInfo pointers outside of the lock.
  auto* primary_host_info = [&]() {
    absl::ReaderMutexLock reader_lock{&primary_hosts_lock_};
    const auto primary_host_it = primary_hosts_.find(host);
    ASSERT(primary_host_it != primary_hosts_.end());
    return primary_host_it->second.get();
  }();

  const bool first_resolve = !primary_host_info->host_info_->firstResolveComplete();
  primary_host_info->timeout_timer_->disableTimer();
  primary_host_info->active_query_ = nullptr;

  // If the DNS resolver successfully resolved with an empty response list, the dns cache does not
  // update. This ensures that a potentially previously resolved address does not stabilize back to
  // 0 hosts.
  const auto new_address = !response.empty()
                               ? Network::Utility::getAddressWithPort(*(response.front().address_),
                                                                      primary_host_info->port_)
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
  auto current_address = primary_host_info->host_info_->address();
  if (new_address != nullptr && (current_address == nullptr || *current_address != *new_address)) {
    ENVOY_LOG(debug, "host '{}' address has changed", host);
    primary_host_info->host_info_->setAddress(new_address);
    runAddUpdateCallbacks(host, primary_host_info->host_info_);
    address_changed = true;
    stats_.host_address_changed_.inc();
  }

  if (first_resolve || address_changed) {
    primary_host_info->host_info_->setFirstResolveComplete();
    notifyThreads(host, primary_host_info->host_info_);
  }

  // Kick off the refresh timer.
  // TODO(mattklein123): Consider jitter here. It may not be necessary since the initial host
  // is populated dynamically.
  if (status == Network::DnsResolver::ResolutionStatus::Success) {
    failure_backoff_strategy_->reset();
    primary_host_info->refresh_timer_->enableTimer(refresh_interval_);
    ENVOY_LOG(debug, "DNS refresh rate reset for host '{}', refresh rate {} ms", host,
              refresh_interval_.count());
  } else {
    const uint64_t refresh_interval = failure_backoff_strategy_->nextBackOffMs();
    primary_host_info->refresh_timer_->enableTimer(std::chrono::milliseconds(refresh_interval));
    ENVOY_LOG(debug, "DNS refresh rate reset for host '{}', (failure) refresh rate {} ms", host,
              refresh_interval);
  }
}

void DnsCacheImpl::runAddUpdateCallbacks(const std::string& host,
                                         const DnsHostInfoSharedPtr& host_info) {
  for (auto* callbacks : update_callbacks_) {
    callbacks->callbacks_.onDnsHostAddOrUpdate(host, host_info);
  }
}

void DnsCacheImpl::runRemoveCallbacks(const std::string& host) {
  for (auto* callbacks : update_callbacks_) {
    callbacks->callbacks_.onDnsHostRemove(host);
  }
}

void DnsCacheImpl::notifyThreads(const std::string& host,
                                 const DnsHostInfoImplSharedPtr& resolved_info) {
  auto shared_info = std::make_shared<HostMapUpdateInfo>(host, resolved_info);
  tls_slot_.runOnAllThreads([shared_info](OptRef<ThreadLocalHostInfo> local_host_info) {
    local_host_info->onHostMapUpdate(shared_info);
  });
}

DnsCacheImpl::ThreadLocalHostInfo::~ThreadLocalHostInfo() {
  // Make sure we cancel any handles that still exist.
  for (const auto& per_host_list : pending_resolutions_) {
    for (auto pending_resolution : per_host_list.second) {
      pending_resolution->cancel();
    }
  }
}

void DnsCacheImpl::ThreadLocalHostInfo::onHostMapUpdate(
    const HostMapUpdateInfoSharedPtr& resolved_host) {
  auto host_it = pending_resolutions_.find(resolved_host->host_);
  if (host_it != pending_resolutions_.end()) {
    for (auto* resolution : host_it->second) {
      auto& callbacks = resolution->callbacks_;
      resolution->cancel();
      callbacks.onLoadDnsCacheComplete(resolved_host->info_);
    }
    pending_resolutions_.erase(host_it);
  }
}

DnsCacheImpl::PrimaryHostInfo::PrimaryHostInfo(DnsCacheImpl& parent,
                                               absl::string_view host_to_resolve, uint16_t port,
                                               bool is_ip_address,
                                               const Event::TimerCb& refresh_timer_cb,
                                               const Event::TimerCb& timeout_timer_cb)
    : parent_(parent), port_(port),
      refresh_timer_(parent.main_thread_dispatcher_.createTimer(refresh_timer_cb)),
      timeout_timer_(parent.main_thread_dispatcher_.createTimer(timeout_timer_cb)),
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
