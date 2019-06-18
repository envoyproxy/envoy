#include "extensions/common/dynamic_forward_proxy/dns_cache_impl.h"

#include "common/network/utility.h"

// TODO(mattklein123): Move DNS family helpers to a smaller include.
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

// TODO(mattklein123): circuit breakers / maximums on the number of hosts that the cache can
//                     contain.
// TODO(mattklein123): stats

DnsCacheImpl::DnsCacheImpl(
    Event::Dispatcher& main_thread_dispatcher, ThreadLocal::SlotAllocator& tls,
    const envoy::config::common::dynamic_forward_proxy::v2alpha::DnsCacheConfig& config)
    : main_thread_dispatcher_(main_thread_dispatcher),
      dns_lookup_family_(Upstream::getDnsLookupFamilyFromEnum(config.dns_lookup_family())),
      resolver_(main_thread_dispatcher.createDnsResolver({})), tls_slot_(tls.allocateSlot()),
      refresh_interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, dns_refresh_rate, 60000)),
      host_ttl_(PROTOBUF_GET_MS_OR_DEFAULT(config, host_ttl, 300000)) {
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

DnsCacheImpl::LoadDnsCacheHandlePtr DnsCacheImpl::loadDnsCache(absl::string_view host,
                                                               uint16_t default_port,
                                                               LoadDnsCacheCallbacks& callbacks) {
  ENVOY_LOG(debug, "thread local lookup for host '{}'", host);
  auto& tls_host_info = tls_slot_->getTyped<ThreadLocalHostInfo>();
  auto tls_host = tls_host_info.host_map_->find(host);
  if (tls_host != tls_host_info.host_map_->end()) {
    ENVOY_LOG(debug, "thread local hit for host '{}'", host);
    return nullptr;
  } else {
    ENVOY_LOG(debug, "thread local miss for host '{}', posting to main thread", host);
    main_thread_dispatcher_.post(
        [this, host = std::string(host), default_port]() { startCacheLoad(host, default_port); });
    return std::make_unique<LoadDnsCacheHandleImpl>(tls_host_info.pending_resolutions_, host,
                                                    callbacks);
  }
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

  // TODO(mattklein123): Figure out if we want to support addresses of the form <IP>:<port>. This
  //                     seems unlikely to be useful in TLS scenarios, but it is technically
  //                     supported. We might want to block this form for now.
  const auto colon_pos = host.find(':');
  absl::string_view host_to_resolve = host;
  if (colon_pos != absl::string_view::npos) {
    const absl::string_view string_view_host = host;
    host_to_resolve = string_view_host.substr(0, colon_pos);
    const auto port_str = string_view_host.substr(colon_pos + 1);
    uint64_t port64;
    if (port_str.empty() || !absl::SimpleAtoi(port_str, &port64) || port64 > 65535) {
      // Just attempt to resolve whatever we were given. This will very likely fail.
      // TODO(mattklein123): Should we actually fail here or do something different?
      host_to_resolve = host;
    } else {
      default_port = port64;
    }
  }

  // TODO(mattklein123): Right now, the same host with different ports will become two
  // independent primary hosts with independent DNS resolutions. I'm not sure how much this will
  // matter, but we could consider collapsing these down and sharing the underlying DNS resolution.
  auto& primary_host =
      *primary_hosts_
           // try_emplace() is used here for direct argument forwarding.
           .try_emplace(host,
                        std::make_unique<PrimaryHostInfo>(*this, host_to_resolve, default_port,
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
    runRemoveCallbacks(host);
    primary_hosts_.erase(primary_host_it);
    updateTlsHostsMap();
  } else {
    startResolve(host, *primary_host_it->second);
  }
}

void DnsCacheImpl::startResolve(const std::string& host, PrimaryHostInfo& host_info) {
  ENVOY_LOG(debug, "starting main thread resolve for host='{}' dns='{}' port='{}'", host,
            host_info.host_to_resolve_, host_info.port_);
  ASSERT(host_info.active_query_ == nullptr);
  host_info.active_query_ = resolver_->resolve(
      host_info.host_to_resolve_, dns_lookup_family_,
      [this, host](const std::list<Network::Address::InstanceConstSharedPtr>&& address_list) {
        finishResolve(host, address_list);
      });
}

void DnsCacheImpl::finishResolve(
    const std::string& host,
    const std::list<Network::Address::InstanceConstSharedPtr>& address_list) {
  ENVOY_LOG(debug, "main thread resolve complete for host '{}'. {} results", host,
            address_list.size());
  const auto primary_host_it = primary_hosts_.find(host);
  ASSERT(primary_host_it != primary_hosts_.end());

  auto& primary_host_info = *primary_host_it->second;
  primary_host_info.active_query_ = nullptr;
  const bool first_resolve = primary_host_info.host_info_ == nullptr;
  if (primary_host_info.host_info_ == nullptr) {
    primary_host_info.host_info_ =
        std::make_shared<DnsHostInfoImpl>(main_thread_dispatcher_.timeSource());
  }

  const auto new_address =
      !address_list.empty()
          ? Network::Utility::getAddressWithPort(*address_list.front(), primary_host_info.port_)
          : nullptr;

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
  }

  if (first_resolve || address_changed) {
    updateTlsHostsMap();
  }

  // Kick off the refresh timer.
  // TODO(mattklein123): Consider jitter here. It may not be necessary since the initial host
  // is populated dynamically.
  primary_host_info.refresh_timer_->enableTimer(refresh_interval_);
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
    // Do not include hosts without host info. This only happens before we get the first
    // resolution.
    if (primary_host.second->host_info_ != nullptr) {
      new_host_map->emplace(primary_host.first, primary_host.second->host_info_);
    }
  }

  tls_slot_->runOnAllThreads([this, new_host_map]() {
    tls_slot_->getTyped<ThreadLocalHostInfo>().updateHostMap(new_host_map);
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

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
