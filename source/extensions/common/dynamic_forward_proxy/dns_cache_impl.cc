#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "source/common/common/dns_utils.h"
#include "source/common/common/stl_helpers.h"
#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

absl::StatusOr<std::shared_ptr<DnsCacheImpl>> DnsCacheImpl::createDnsCacheImpl(
    Server::Configuration::GenericFactoryContext& context,
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) {
  const uint32_t max_hosts = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_hosts, 1024);
  if (static_cast<size_t>(config.preresolve_hostnames().size()) > max_hosts) {
    return absl::InvalidArgumentError(fmt::format(
        "DNS Cache [{}] configured with preresolve_hostnames={} larger than max_hosts={}",
        config.name(), config.preresolve_hostnames().size(), max_hosts));
  }
  auto resolver_or_error =
      selectDnsResolver(config, context.serverFactoryContext().mainThreadDispatcher(),
                        context.serverFactoryContext());
  RETURN_IF_NOT_OK_REF(resolver_or_error.status());

  return std::shared_ptr<DnsCacheImpl>(
      new DnsCacheImpl(context, config, std::move(*resolver_or_error)));
}

DnsCacheImpl::DnsCacheImpl(
    Server::Configuration::GenericFactoryContext& context,
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config,
    Network::DnsResolverSharedPtr&& resolver)
    : main_thread_dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      config_(config), random_generator_(context.serverFactoryContext().api().randomGenerator()),
      dns_lookup_family_(DnsUtils::getDnsLookupFamilyFromEnum(config.dns_lookup_family())),
      resolver_(std::move(resolver)), tls_slot_(context.serverFactoryContext().threadLocal()),
      scope_(context.scope().createScope(fmt::format("dns_cache.{}.", config.name()))),
      stats_(generateDnsCacheStats(*scope_)),
      resource_manager_(*scope_, context.serverFactoryContext().runtime(), config.name(),
                        config.dns_cache_circuit_breaker()),
      refresh_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, dns_refresh_rate, 60000)),
      min_refresh_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, dns_min_refresh_rate, 5000)),
      timeout_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, dns_query_timeout, 5000)),
      file_system_(context.serverFactoryContext().api().fileSystem()),
      validation_visitor_(context.messageValidationVisitor()),
      host_ttl_(PROTOBUF_GET_MS_OR_DEFAULT(config, host_ttl, 300000)),
      max_hosts_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_hosts, 1024)) {
  tls_slot_.set([&](Event::Dispatcher&) { return std::make_shared<ThreadLocalHostInfo>(*this); });

  loadCacheEntries(config);

  // Preresolved hostnames are resolved without a read lock on primary hosts because it is done
  // during object construction.
  for (const auto& hostname : config.preresolve_hostnames()) {
    // No need to get a resolution handle on this resolution as the only outcome needed is for the
    // cache to load an entry. Further if this particular resolution fails all the is lost is the
    // potential optimization of having the entry be preresolved the first time a true consumer of
    // this DNS cache asks for it.
    const std::string host =
        DnsHostInfo::normalizeHostForDfp(hostname.address(), hostname.port_value());
    ENVOY_LOG(debug, "DNS pre-resolve starting for host {}", host);
    startCacheLoad(host, hostname.port_value(), false, false);
  }
  enable_dfp_dns_trace_ = context.serverFactoryContext().runtime().snapshot().getBoolean(
      "envoy.enable_dfp_dns_trace", false);
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

absl::StatusOr<Network::DnsResolverSharedPtr> DnsCacheImpl::selectDnsResolver(
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config,
    Event::Dispatcher& main_thread_dispatcher,
    Server::Configuration::CommonFactoryContext& context) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  Network::DnsResolverFactory* dns_resolver_factory;

  // If DnsCacheConfig doesn't have any DNS related configuration, and the
  // default DNS resolver, i.e, the typed_dns_resolver_config in the bootstrap
  // configuration, is not empty, then creates the default DNS resolver.
  if (!config.has_typed_dns_resolver_config() && !config.has_dns_resolution_config() &&
      context.api().bootstrap().has_typed_dns_resolver_config() &&
      !(context.api().bootstrap().typed_dns_resolver_config().typed_config().type_url().empty())) {
    typed_dns_resolver_config = context.api().bootstrap().typed_dns_resolver_config();
    dns_resolver_factory =
        &Network::createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
  } else {
    dns_resolver_factory =
        &Network::createDnsResolverFactoryFromProto(config, typed_dns_resolver_config);
  }
  return dns_resolver_factory->createDnsResolver(main_thread_dispatcher, context.api(),
                                                 typed_dns_resolver_config);
}

DnsCacheStats DnsCacheImpl::generateDnsCacheStats(Stats::Scope& scope) {
  return {ALL_DNS_CACHE_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

DnsCacheImpl::LoadDnsCacheEntryResult
DnsCacheImpl::loadDnsCacheEntryWithForceRefresh(absl::string_view raw_host, uint16_t default_port,
                                                bool is_proxy_lookup, bool force_refresh,
                                                LoadDnsCacheEntryCallbacks& callbacks) {
  std::string host = DnsHostInfo::normalizeHostForDfp(raw_host, default_port);

  ENVOY_LOG(debug, "thread local lookup for host '{}' {}", host,
            is_proxy_lookup ? "proxy mode " : "");
  ThreadLocalHostInfo& tls_host_info = *tls_slot_;

  bool is_overflow = false;
  absl::optional<DnsHostInfoSharedPtr> host_info = absl::nullopt;
  bool ignore_cached_entries = force_refresh;

  {
    absl::ReaderMutexLock read_lock{primary_hosts_lock_};
    is_overflow = primary_hosts_.size() >= max_hosts_;
    auto tls_host = primary_hosts_.find(host);
    if (tls_host != primary_hosts_.end() && tls_host->second->host_info_->firstResolveComplete()) {
      host_info = tls_host->second->host_info_;
    }
  }

  if (host_info) {
    ENVOY_LOG(debug, "cache hit for host '{}'", host);
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.reresolve_null_addresses") &&
        !is_proxy_lookup && *host_info && (*host_info)->address() == nullptr) {
      ENVOY_LOG(debug, "ignoring null address cache hit for miss for host '{}'", host);
      ignore_cached_entries = true;
    }
    if (config_.disable_dns_refresh_on_failure() && !is_proxy_lookup &&
        (*host_info)->resolutionStatus() == Network::DnsResolver::ResolutionStatus::Failure) {
      ENVOY_LOG(debug, "ignoring failed address cache hit for miss for host '{}'", host);
      ignore_cached_entries = true;
    }
    if (!ignore_cached_entries) {
      return {LoadDnsCacheEntryStatus::InCache, nullptr, host_info};
    }
  }
  if (is_overflow) {
    ENVOY_LOG(debug, "DNS cache overflow for host '{}'", host);
    stats_.host_overflow_.inc();
    return {LoadDnsCacheEntryStatus::Overflow, nullptr, absl::nullopt};
  }
  ENVOY_LOG(debug, "cache miss for host '{}', posting to main thread", host);
  main_thread_dispatcher_.post(
      [this, host = std::string(host), default_port, is_proxy_lookup, ignore_cached_entries]() {
        startCacheLoad(host, default_port, is_proxy_lookup, ignore_cached_entries);
      });
  return {LoadDnsCacheEntryStatus::Loading,
          std::make_unique<LoadDnsCacheEntryHandleImpl>(tls_host_info.pending_resolutions_, host,
                                                        callbacks),
          absl::nullopt};
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
  absl::ReaderMutexLock reader_lock{primary_hosts_lock_};
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
    absl::ReaderMutexLock reader_lock{primary_hosts_lock_};
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

void DnsCacheImpl::startCacheLoad(const std::string& host, uint16_t default_port,
                                  bool is_proxy_lookup, bool ignore_cached_entries) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());

  // It's possible for multiple requests to race trying to start a resolution. If a host is
  // already in the map it's either in the process of being resolved or the resolution is already
  // heading out to the worker threads. Either way the pending resolution will be completed.

  // Functions like this one that modify primary_hosts_ are only called in the main thread so we
  // know it is safe to use the PrimaryHostInfo pointers outside of the lock.
  auto* primary_host = [&]() {
    absl::ReaderMutexLock reader_lock{primary_hosts_lock_};
    auto host_it = primary_hosts_.find(host);
    return host_it != primary_hosts_.end() ? host_it->second.get() : nullptr;
  }();

  if (primary_host) {
    if (!ignore_cached_entries || !primary_host->host_info_->firstResolveComplete()) {
      ENVOY_LOG(debug, "main thread resolve for host '{}' skipped. Entry present", host);
      return;
    }
    // The host was in cache but we want to force a refresh. Remove the host
    // entirely to ensure initial resolve logic works as expected.
    removeHost(host, *primary_host, false);
  }

  primary_host = createHost(host, default_port);
  // If the DNS request was simply to create a host endpoint in a Dynamic Forward Proxy cluster,
  // fast fail the look-up as the address is not needed.
  if (is_proxy_lookup) {
    finishResolve(host, Network::DnsResolver::ResolutionStatus::Completed, "proxy_resolve", {}, {},
                  true);
  } else {
    startResolve(host, *primary_host);
  }
}

DnsCacheImpl::PrimaryHostInfo* DnsCacheImpl::createHost(const std::string& host,
                                                        uint16_t default_port) {
  const auto host_attributes = Http::Utility::parseAuthority(host);
  // TODO(mattklein123): Right now, the same host with different ports will become two
  // independent primary hosts with independent DNS resolutions. I'm not sure how much this will
  // matter, but we could consider collapsing these down and sharing the underlying DNS resolution.
  {
    absl::WriterMutexLock writer_lock{primary_hosts_lock_};
    return primary_hosts_
        // try_emplace() is used here for direct argument forwarding.
        .try_emplace(host,
                     std::make_unique<PrimaryHostInfo>(
                         *this, std::string(host_attributes.host_),
                         host_attributes.port_.value_or(default_port),
                         host_attributes.is_ip_address_, [this, host]() { onReResolveAlarm(host); },
                         [this, host]() { onResolveTimeout(host); }))
        .first->second.get();
  }
}

DnsCacheImpl::PrimaryHostInfo& DnsCacheImpl::getPrimaryHost(const std::string& host) {
  // Functions modify primary_hosts_ are only called in the main thread so we
  // know it is safe to use the PrimaryHostInfo pointers outside of the lock.
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  absl::ReaderMutexLock reader_lock{primary_hosts_lock_};
  const auto primary_host_it = primary_hosts_.find(host);
  ASSERT(primary_host_it != primary_hosts_.end());
  return *(primary_host_it->second);
}

void DnsCacheImpl::onResolveTimeout(const std::string& host) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());

  ENVOY_LOG_EVENT(debug, "dns_cache_resolve_timeout", "host='{}' resolution timeout", host);
  stats_.dns_query_timeout_.inc();
  finishResolve(host, Network::DnsResolver::ResolutionStatus::Failure, "resolve_timeout", {},
                absl::nullopt, /* is_proxy_lookup= */ false, /* is_timeout= */ true);
}

void DnsCacheImpl::onReResolveAlarm(const std::string& host) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());

  auto& primary_host = getPrimaryHost(host);
  const std::chrono::steady_clock::duration now_duration =
      main_thread_dispatcher_.timeSource().monotonicTime().time_since_epoch();
  auto last_used_time = primary_host.host_info_->lastUsedTime();
  ENVOY_LOG(debug, "host='{}' TTL check: now={} last_used={} TTL {}", host, now_duration.count(),
            last_used_time.count(), host_ttl_.count());
  if ((now_duration - last_used_time) > host_ttl_) {
    ENVOY_LOG(debug, "host='{}' TTL expired, removing", host);
    removeHost(host, primary_host, true);
  } else {
    startResolve(host, primary_host);
  }
}

void DnsCacheImpl::removeHost(const std::string& host, const PrimaryHostInfo& primary_host,
                              bool update_threads) {
  // If we need to erase the host, hold onto the PrimaryHostInfo object that owns this callback.
  // This is defined at function scope so that it is only erased on function exit to avoid
  // use-after-free issues
  PrimaryHostInfoPtr host_to_erase;

  // If the host has no address then that means that the DnsCacheImpl has never
  // runAddUpdateCallbacks for this host, and thus the callback targets are not aware of it.
  // Therefore, runRemoveCallbacks should only be ran if the host's address != nullptr.
  if (primary_host.host_info_->address()) {
    runRemoveCallbacks(host);
  }
  {
    removeCacheEntry(host);
    absl::WriterMutexLock writer_lock{primary_hosts_lock_};
    auto host_it = primary_hosts_.find(host);
    ASSERT(host_it != primary_hosts_.end());
    host_to_erase = std::move(host_it->second);
    primary_hosts_.erase(host_it);
  }
  // In the case of force-remove and resolve, don't cancel outstanding resolve
  // callbacks on remove, as a resolve is pending.
  if (update_threads) {
    notifyThreads(host, primary_host.host_info_);
  }
}

void DnsCacheImpl::forceRefreshHosts() {
  ENVOY_LOG(debug, "beginning DNS cache force refresh");
  // Tell the underlying resolver to reset itself since we likely just went through a network
  // transition and parameters may have changed.
  resolver_->resetNetworking();

  absl::ReaderMutexLock reader_lock{primary_hosts_lock_};
  for (auto& primary_host : primary_hosts_) {
    // Avoid holding the lock for longer than necessary by just triggering the refresh timer for
    // each host IFF the host is not already refreshing. Cancellation is assumed to be cheap for
    // resolvers.
    if (primary_host.second->active_query_ != nullptr) {
      primary_host.second->active_query_->cancel(
          Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
      primary_host.second->active_query_ = nullptr;
      if (timeout_interval_.count() > 0) {
        primary_host.second->timeout_timer_->disableTimer();
      }
    }

    if (timeout_interval_.count() > 0) {
      ASSERT(!primary_host.second->timeout_timer_->enabled());
    }
    primary_host.second->refresh_timer_->enableTimer(std::chrono::milliseconds(0), nullptr);
    ENVOY_LOG_EVENT(debug, "force_refresh_host", "force refreshing host='{}'", primary_host.first);
  }
}

void DnsCacheImpl::setIpVersionToRemove(absl::optional<Network::Address::IpVersion> ip_version) {
  bool has_changed = false;
  {
    absl::MutexLock lock{ip_version_to_remove_lock_};
    has_changed = ip_version_to_remove_ != ip_version;
    ip_version_to_remove_ = ip_version;
  }

  if (has_changed && Runtime::runtimeFeatureEnabled(
                         "envoy.reloadable_features.dns_cache_filter_unusable_ip_version")) {
    // The IP version to remove has changed, so we need to refresh all logical hosts in the DFP
    // cluster so they filter out the unsupported/unusable IP addresses from their address list.
    absl::ReaderMutexLock reader_lock{primary_hosts_lock_};
    for (auto& primary_host : primary_hosts_) {
      for (auto* callbacks : update_callbacks_) {
        auto status = callbacks->callbacks_.onDnsHostAddOrUpdate(primary_host.first,
                                                                 primary_host.second->host_info_);
        if (!status.ok()) {
          // TODO(abeyad): Do something better with a failure status.
          ENVOY_LOG(warn, "Failed to update DFP host after IP version update due to {}",
                    status.message());
        }
      }
    }
    ENVOY_LOG(debug, "refresh all {} logical hosts in host map, unsupported IP version {}",
              primary_hosts_.size(),
              ip_version.has_value()
                  ? (*ip_version == Network::Address::IpVersion::v4 ? "v4" : "v6")
                  : "none");
  }
}

absl::optional<Network::Address::IpVersion> DnsCacheImpl::getIpVersionToRemove() {
  absl::MutexLock lock{ip_version_to_remove_lock_};
  return ip_version_to_remove_;
}

void DnsCacheImpl::stop() {
  ENVOY_LOG(debug, "stopping DNS cache");
  // Tell the underlying resolver to reset itself since we likely just went through a network
  // transition and parameters may have changed.
  resolver_->resetNetworking();

  absl::ReaderMutexLock reader_lock{primary_hosts_lock_};
  for (auto& primary_host : primary_hosts_) {
    if (primary_host.second->active_query_ != nullptr) {
      primary_host.second->active_query_->cancel(
          Network::ActiveDnsQuery::CancelReason::QueryAbandoned);
      primary_host.second->active_query_ = nullptr;
    }

    if (timeout_interval_.count() > 0) {
      primary_host.second->timeout_timer_->disableTimer();
      ASSERT(!primary_host.second->timeout_timer_->enabled());
    }
    primary_host.second->refresh_timer_->disableTimer();
    ENVOY_LOG_EVENT(debug, "stop_host", "stop host='{}'", primary_host.first);
  }
}

void DnsCacheImpl::startResolve(const std::string& host, PrimaryHostInfo& host_info) {
  ENVOY_LOG(debug, "starting main thread resolve for host='{}' dns='{}' port='{}' timeout='{}'",
            host, host_info.host_info_->resolvedHost(), host_info.port_, timeout_interval_.count());
  ASSERT(host_info.active_query_ == nullptr);

  stats_.dns_query_attempt_.inc();
  if (timeout_interval_.count() > 0) {
    host_info.timeout_timer_->enableTimer(timeout_interval_, nullptr);
  }
  host_info.active_query_ = resolver_->resolve(
      host_info.host_info_->resolvedHost(), dns_lookup_family_,
      [this, host](Network::DnsResolver::ResolutionStatus status, absl::string_view details,
                   std::list<Network::DnsResponse>&& response) {
        finishResolve(host, status, details, std::move(response));
      });
}

void DnsCacheImpl::finishResolve(const std::string& host,
                                 Network::DnsResolver::ResolutionStatus status,
                                 absl::string_view details,
                                 std::list<Network::DnsResponse>&& response,
                                 absl::optional<MonotonicTime> resolution_time,
                                 bool is_proxy_lookup, bool is_timeout) {
  ASSERT(main_thread_dispatcher_.isThreadSafe());
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.dns_cache_set_ip_version_to_remove")) {
    {
      absl::MutexLock lock{ip_version_to_remove_lock_};
      if (ip_version_to_remove_.has_value()) {
        if (config_.preresolve_hostnames_size() > 0) {
          IS_ENVOY_BUG(
              "Unable to delete IP version addresses when DNS preresolve hostnames are not empty.");
        } else {
          response.remove_if([ip_version_to_remove =
                                  *ip_version_to_remove_](const Network::DnsResponse& dns_resp) {
            // Ignore the loopback address because a socket interface can still support both IPv4
            // and IPv6 but has no outgoing IPv4/IPv6 connectivity.
            return !Network::Utility::isLoopbackAddress(*dns_resp.addrInfo().address_) &&
                   dns_resp.addrInfo().address_->ip()->version() == ip_version_to_remove;
          });
        }
      }
    }
  }
  ENVOY_LOG_EVENT(debug, "dns_cache_finish_resolve",
                  "main thread resolve complete for host '{}': {}", host,
                  accumulateToString<Network::DnsResponse>(response, [](const auto& dns_response) {
                    return dns_response.addrInfo().address_->asString();
                  }));
  const bool from_cache = resolution_time.has_value();

  // Functions like this one that modify primary_hosts_ are only called in the main thread so we
  // know it is safe to use the PrimaryHostInfo pointers outside of the lock.
  auto* primary_host_info = [&]() {
    absl::ReaderMutexLock reader_lock{primary_hosts_lock_};
    const auto primary_host_it = primary_hosts_.find(host);
    ASSERT(primary_host_it != primary_hosts_.end());
    return primary_host_it->second.get();
  }();

  std::string details_with_maybe_trace = std::string(details);
  if (primary_host_info != nullptr && primary_host_info->active_query_ != nullptr) {
    if (enable_dfp_dns_trace_) {
      std::string traces = primary_host_info->active_query_->getTraces();
      details_with_maybe_trace = absl::StrCat(details, ":", traces);
    }
    // `cancel` must be called last because the `ActiveQuery` will be destroyed afterward.
    if (is_timeout) {
      primary_host_info->active_query_->cancel(Network::ActiveDnsQuery::CancelReason::Timeout);
    }
  }
  bool failure = status == Network::DnsResolver::ResolutionStatus::Failure || response.empty();
  details_with_maybe_trace = absl::StrCat(
      (failure ? "dns_resolution_failure{" : ""),
      StringUtil::replaceAllEmptySpace(details_with_maybe_trace), (failure ? "}" : ""));

  bool first_resolve = false;

  if (!from_cache) {
    first_resolve = !primary_host_info->host_info_->firstResolveComplete();
    if (timeout_interval_.count() > 0) {
      primary_host_info->timeout_timer_->disableTimer();
    }
    primary_host_info->active_query_ = nullptr;

    if (status == Network::DnsResolver::ResolutionStatus::Failure) {
      stats_.dns_query_failure_.inc();
    } else {
      stats_.dns_query_success_.inc();
    }
  }

  auto address_list = DnsUtils::generateAddressList(response, primary_host_info->port_);
  // Only the change the address if:
  // 1) The new address is valid &&
  // 2a) The host doesn't yet have an address ||
  // 2b) The host has a changed address.
  //
  // This means that once a host gets an address it will stick even in the case of a subsequent
  // resolution failure.
  bool address_changed = false;
  auto current_address = primary_host_info->host_info_->address();

  if (!resolution_time.has_value()) {
    resolution_time = main_thread_dispatcher_.timeSource().monotonicTime();
  }
  std::chrono::seconds dns_ttl =
      std::chrono::duration_cast<std::chrono::seconds>(refresh_interval_);

  // If the DNS resolver successfully resolved with an empty response list, the dns cache does not
  // update. This ensures that a potentially previously resolved address does not stabilize back to
  // 0 hosts.
  if (!address_list.empty()) {
    // Update the cache entry and staleness any time the ttl changes.
    if (!from_cache) {
      addCacheEntry(host, address_list, response.front().addrInfo().ttl_);
    }
    // Arbitrarily cap DNS re-resolution at min_refresh_interval_ to avoid constant DNS queries.
    dns_ttl = std::max<std::chrono::seconds>(
        std::chrono::duration_cast<std::chrono::seconds>(min_refresh_interval_),
        response.front().addrInfo().ttl_);
    primary_host_info->host_info_->updateStale(resolution_time.value(), dns_ttl);
  }

  bool should_update_cache =
      !address_list.empty() &&
      DnsUtils::listChanged(address_list,
                            primary_host_info->host_info_->addressList(/*filtered=*/false));
  // If this was a proxy lookup it's OK to send a null address resolution as
  // long as this isn't a transition from non-null to null address.
  should_update_cache |= is_proxy_lookup && !current_address;

  if (should_update_cache) {
    primary_host_info->host_info_->setAddresses(std::move(address_list), details_with_maybe_trace,
                                                status);
    ENVOY_LOG_EVENT(debug, "dns_cache_update_address",
                    "host '{}' address has changed from {} to {}", host,
                    current_address ? current_address->asStringView() : "<empty>",
                    primary_host_info->host_info_->address()
                        ? primary_host_info->host_info_->address()->asStringView()
                        : "<empty>");

    absl::Status host_status = runAddUpdateCallbacks(host, primary_host_info->host_info_);
    ENVOY_BUG(host_status.ok(),
              absl::StrCat("Failed to update DFP host due to ", host_status.message()));
    primary_host_info->host_info_->setFirstResolveComplete();
    address_changed = true;
    stats_.host_address_changed_.inc();
  } else if (current_address == nullptr) {
    // We only set details here if current address is null because but
    // non-null->null resolutions we don't update the address so will use a
    // previously resolved address + details.
    primary_host_info->host_info_->setDetails(details_with_maybe_trace);
    primary_host_info->host_info_->setResolutionStatus(status);
  }

  if (first_resolve) {
    primary_host_info->host_info_->setFirstResolveComplete();
  }
  if (first_resolve || (address_changed && !primary_host_info->host_info_->isStale())) {
    notifyThreads(host, primary_host_info->host_info_);
  }

  runResolutionCompleteCallbacks(host, primary_host_info->host_info_, status);

  // Kick off the refresh timer.
  if (status == Network::DnsResolver::ResolutionStatus::Completed) {
    primary_host_info->failure_backoff_strategy_->reset();
    primary_host_info->refresh_timer_->enableTimer(dns_ttl);
    ENVOY_LOG(debug, "DNS refresh rate reset for host '{}', refresh rate {} ms", host,
              dns_ttl.count() * 1000);
  } else {
    if (!config_.disable_dns_refresh_on_failure()) {
      const uint64_t refresh_interval =
          primary_host_info->failure_backoff_strategy_->nextBackOffMs();
      primary_host_info->refresh_timer_->enableTimer(std::chrono::milliseconds(refresh_interval));
      ENVOY_LOG(debug, "DNS refresh rate reset for host '{}', (failure) refresh rate {} ms", host,
                refresh_interval);
    }
  }
}

absl::Status DnsCacheImpl::runAddUpdateCallbacks(const std::string& host,
                                                 const DnsHostInfoSharedPtr& host_info) {
  for (auto* callbacks : update_callbacks_) {
    RETURN_IF_NOT_OK(callbacks->callbacks_.onDnsHostAddOrUpdate(host, host_info));
  }
  return absl::OkStatus();
}

void DnsCacheImpl::runResolutionCompleteCallbacks(const std::string& host,
                                                  const DnsHostInfoSharedPtr& host_info,
                                                  Network::DnsResolver::ResolutionStatus status) {
  for (auto* callbacks : update_callbacks_) {
    callbacks->callbacks_.onDnsResolutionComplete(host, host_info, status);
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
    // Calling the onLoadDnsCacheComplete may trigger more host resolutions adding more elements
    // to the `pending_resolutions_` map, potentially invalidating the host_it iterator. So we
    // copy the list of handles to a local variable before cleaning up the map.
    std::list<LoadDnsCacheEntryHandleImpl*> completed_resolutions(std::move(host_it->second));
    pending_resolutions_.erase(host_it);
    for (auto* resolution : completed_resolutions) {
      auto& callbacks = resolution->callbacks_;
      resolution->cancel();
      callbacks.onLoadDnsCacheComplete(resolved_host->info_);
    }
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
      host_info_(std::make_shared<DnsHostInfoImpl>(parent, host_to_resolve, is_ip_address)),
      failure_backoff_strategy_(
          Config::Utility::prepareDnsRefreshStrategy<
              envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig>(
              parent_.config_, parent_.refresh_interval_.count(), parent_.random_generator_)) {
  parent_.stats_.host_added_.inc();
  parent_.stats_.num_hosts_.inc();
}

DnsCacheImpl::PrimaryHostInfo::~PrimaryHostInfo() {
  parent_.stats_.host_removed_.inc();
  parent_.stats_.num_hosts_.dec();
}

void DnsCacheImpl::addCacheEntry(
    const std::string& host,
    const std::vector<Network::Address::InstanceConstSharedPtr>& address_list,
    const std::chrono::seconds ttl) {
  if (!key_value_store_ || address_list.empty()) {
    return;
  }
  MonotonicTime now = main_thread_dispatcher_.timeSource().monotonicTime();
  uint64_t seconds_since_epoch =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  std::string value = absl::StrJoin(address_list, "\n", [&](std::string* out, const auto& addr) {
    absl::StrAppend(out, addr->asString(), "|", ttl.count(), "|", seconds_since_epoch);
  });
  key_value_store_->addOrUpdate(host, value, absl::nullopt);
}

void DnsCacheImpl::removeCacheEntry(const std::string& host) {
  if (!key_value_store_) {
    return;
  }
  key_value_store_->remove(host);
}

absl::optional<Network::DnsResponse>
DnsCacheImpl::parseValue(absl::string_view value, absl::optional<MonotonicTime>& resolution_time) {
  Network::Address::InstanceConstSharedPtr address;
  const auto parts = StringUtil::splitToken(value, "|");
  std::chrono::seconds ttl(0);
  if (parts.size() != 3) {
    ENVOY_LOG(warn, "Incorrect number of tokens in the cache line");
    return {};
  }
  address = Network::Utility::parseInternetAddressAndPortNoThrow(std::string(parts[0]));
  if (address == nullptr) {
    ENVOY_LOG(warn, "{} is not a valid address", parts[0]);
  }
  uint64_t ttl_int;
  if (absl::SimpleAtoi(parts[1], &ttl_int) && ttl_int != 0) {
    ttl = std::chrono::seconds(ttl_int);
  } else {
    ENVOY_LOG(warn, "{} is not a valid ttl", parts[1]);
  }
  uint64_t epoch_int;
  if (absl::SimpleAtoi(parts[2], &epoch_int)) {
    MonotonicTime now = main_thread_dispatcher_.timeSource().monotonicTime();
    const std::chrono::seconds seconds_since_epoch =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    resolution_time = main_thread_dispatcher_.timeSource().monotonicTime() -
                      (seconds_since_epoch - std::chrono::seconds(epoch_int));
  }
  if (address == nullptr || ttl == std::chrono::seconds(0) || !resolution_time.has_value()) {
    ENVOY_LOG(warn, "Unable to parse cache line '{}'", value);
    return {};
  }
  return Network::DnsResponse(address, ttl);
}

void DnsCacheImpl::loadCacheEntries(
    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) {
  if (!config.has_key_value_config()) {
    return;
  }
  auto& factory =
      Config::Utility::getAndCheckFactory<KeyValueStoreFactory>(config.key_value_config().config());
  key_value_store_ = factory.createStore(config.key_value_config(), validation_visitor_,
                                         main_thread_dispatcher_, file_system_);
  KeyValueStore::ConstIterateCb load = [this](const std::string& key, const std::string& value) {
    absl::optional<MonotonicTime> resolution_time;
    std::list<Network::DnsResponse> responses;
    const auto addresses = StringUtil::splitToken(value, "\n");
    for (absl::string_view address_line : addresses) {
      absl::optional<Network::DnsResponse> response = parseValue(address_line, resolution_time);
      if (!response.has_value()) {
        return KeyValueStore::Iterate::Break;
      }
      responses.emplace_back(response.value());
    }
    if (responses.empty()) {
      return KeyValueStore::Iterate::Break;
    }
    createHost(key, responses.front().addrInfo().address_->ip()->port());
    ENVOY_LOG_EVENT(
        debug, "dns_cache_load_finished", "persistent dns cache load complete for host '{}': {}",
        key, accumulateToString<Network::DnsResponse>(responses, [](const auto& dns_response) {
          return dns_response.addrInfo().address_->asString();
        }));
    finishResolve(key, Network::DnsResolver::ResolutionStatus::Completed, "from_cache",
                  std::move(responses), resolution_time);
    stats_.cache_load_.inc();
    return KeyValueStore::Iterate::Continue;
  };
  key_value_store_->iterate(load);
}

DnsCacheImpl::DnsHostInfoImpl::DnsHostInfoImpl(DnsCacheImpl& parent,
                                               absl::string_view resolved_host, bool is_ip_address)
    : parent_(parent), resolved_host_(resolved_host), is_ip_address_(is_ip_address),
      stale_at_time_(parent_.main_thread_dispatcher_.timeSource().monotonicTime()) {
  touch();
}

Network::Address::InstanceConstSharedPtr DnsCacheImpl::DnsHostInfoImpl::address() const {
  const bool filter_unusable_ips = Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.dns_cache_filter_unusable_ip_version");
  absl::optional<Network::Address::IpVersion> ip_version_to_remove = parent_.getIpVersionToRemove();
  absl::ReaderMutexLock lock{resolve_lock_};
  for (const auto& address : address_list_) {
    // If not filtering unusable IPs, OR if there is no IP version to remove, OR if the address is
    // not of the IP family to remove, use the address. This means if the
    // `dns_cache_filter_unusable_ip_version` feature is off OR there is no set IP family to remove,
    // the first address in the list will automatically be returned.
    if (!filter_unusable_ips || !ip_version_to_remove ||
        address->ip()->version() != *ip_version_to_remove) {
      return address;
    }
  }
  // If no address was returned yet, return the first address in the list, if any.
  return !address_list_.empty() ? address_list_.front() : nullptr;
}

std::vector<Network::Address::InstanceConstSharedPtr>
DnsCacheImpl::DnsHostInfoImpl::addressList(const bool filtered) const {
  if (filtered && Runtime::runtimeFeatureEnabled(
                      "envoy.reloadable_features.dns_cache_filter_unusable_ip_version")) {
    auto ip_version_to_remove = parent_.getIpVersionToRemove();
    if (ip_version_to_remove.has_value()) {
      std::vector<Network::Address::InstanceConstSharedPtr> ret;
      absl::ReaderMutexLock lock{resolve_lock_};
      for (const auto& address : address_list_) {
        if (address->ip()->version() != *ip_version_to_remove) {
          ret.push_back(address);
        }
      }
      return ret;
    }
  }
  std::vector<Network::Address::InstanceConstSharedPtr> ret;
  absl::ReaderMutexLock lock{resolve_lock_};
  ret = address_list_;
  return ret;
}

const std::string& DnsCacheImpl::DnsHostInfoImpl::resolvedHost() const { return resolved_host_; }

bool DnsCacheImpl::DnsHostInfoImpl::isIpAddress() const { return is_ip_address_; }

void DnsCacheImpl::DnsHostInfoImpl::touch() {
  last_used_time_ = parent_.main_thread_dispatcher_.timeSource().monotonicTime().time_since_epoch();
}

void DnsCacheImpl::DnsHostInfoImpl::updateStale(MonotonicTime resolution_time,
                                                std::chrono::seconds ttl) {
  stale_at_time_ = resolution_time + ttl;
}

bool DnsCacheImpl::DnsHostInfoImpl::isStale() {
  return parent_.main_thread_dispatcher_.timeSource().monotonicTime() >
         static_cast<MonotonicTime>(stale_at_time_);
}

void DnsCacheImpl::DnsHostInfoImpl::setAddresses(
    std::vector<Network::Address::InstanceConstSharedPtr>&& list, absl::string_view details,
    Network::DnsResolver::ResolutionStatus resolution_status) {
  absl::WriterMutexLock lock{resolve_lock_};
  address_list_ = std::move(list);
  details_ = details;
  resolution_status_ = resolution_status;
}

void DnsCacheImpl::DnsHostInfoImpl::setDetails(absl::string_view details) {
  absl::WriterMutexLock lock{resolve_lock_};
  details_ = details;
}

std::string DnsCacheImpl::DnsHostInfoImpl::details() {
  absl::ReaderMutexLock lock{resolve_lock_};
  return details_;
}

std::chrono::steady_clock::duration DnsCacheImpl::DnsHostInfoImpl::lastUsedTime() const {
  return last_used_time_.load();
}

bool DnsCacheImpl::DnsHostInfoImpl::firstResolveComplete() const {
  absl::ReaderMutexLock lock{resolve_lock_};
  return first_resolve_complete_;
}

void DnsCacheImpl::DnsHostInfoImpl::setFirstResolveComplete() {
  absl::WriterMutexLock lock{resolve_lock_};
  first_resolve_complete_ = true;
}

void DnsCacheImpl::DnsHostInfoImpl::setResolutionStatus(
    Network::DnsResolver::ResolutionStatus resolution_status) {
  absl::WriterMutexLock lock{resolve_lock_};
  resolution_status_ = resolution_status;
}

Network::DnsResolver::ResolutionStatus DnsCacheImpl::DnsHostInfoImpl::resolutionStatus() const {
  absl::WriterMutexLock lock{resolve_lock_};
  return resolution_status_;
}

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
