#pragma once

#include "envoy/common/backoff_strategy.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/http/filter.h"
#include "envoy/network/dns.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/cleanup.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "extensions/common/dynamic_forward_proxy/dns_cache_resource_manager.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

/**
 * All DNS cache stats. @see stats_macros.h
 */
#define ALL_DNS_CACHE_STATS(COUNTER, GAUGE)                                                        \
  COUNTER(dns_query_attempt)                                                                       \
  COUNTER(dns_query_failure)                                                                       \
  COUNTER(dns_query_success)                                                                       \
  COUNTER(host_added)                                                                              \
  COUNTER(host_address_changed)                                                                    \
  COUNTER(host_overflow)                                                                           \
  COUNTER(host_removed)                                                                            \
  COUNTER(dns_rq_pending_overflow)                                                                 \
  GAUGE(num_hosts, NeverImport)

/**
 * Struct definition for all DNS cache stats. @see stats_macros.h
 */
struct DnsCacheStats {
  ALL_DNS_CACHE_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class DnsCacheImpl : public DnsCache, Logger::Loggable<Logger::Id::forward_proxy> {
public:
  DnsCacheImpl(Event::Dispatcher& main_thread_dispatcher, ThreadLocal::SlotAllocator& tls,
               Random::RandomGenerator& random, Runtime::Loader& loader, Stats::Scope& root_scope,
               const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config);
  ~DnsCacheImpl() override;
  static DnsCacheStats generateDnsCacheStats(Stats::Scope& scope);

  // DnsCache
  LoadDnsCacheEntryResult loadDnsCacheEntry(absl::string_view host, uint16_t default_port,
                                            LoadDnsCacheEntryCallbacks& callbacks) override;
  AddUpdateCallbacksHandlePtr addUpdateCallbacks(UpdateCallbacks& callbacks) override;
  absl::flat_hash_map<std::string, DnsHostInfoSharedPtr> hosts() override;
  absl::optional<const DnsHostInfoSharedPtr> getHost(absl::string_view host_name) override;
  Upstream::ResourceAutoIncDecPtr
  canCreateDnsRequest(ResourceLimitOptRef pending_requests) override;

private:
  using TlsHostMap = absl::flat_hash_map<std::string, DnsHostInfoSharedPtr>;
  using TlsHostMapSharedPtr = std::shared_ptr<TlsHostMap>;

  struct LoadDnsCacheEntryHandleImpl : public LoadDnsCacheEntryHandle,
                                       RaiiListElement<LoadDnsCacheEntryHandleImpl*> {
    LoadDnsCacheEntryHandleImpl(std::list<LoadDnsCacheEntryHandleImpl*>& parent,
                                absl::string_view host, LoadDnsCacheEntryCallbacks& callbacks)
        : RaiiListElement<LoadDnsCacheEntryHandleImpl*>(parent, this), host_(host),
          callbacks_(callbacks) {}

    const std::string host_;
    LoadDnsCacheEntryCallbacks& callbacks_;
  };

  // Per-thread DNS cache info including the currently known hosts as well as any pending callbacks.
  struct ThreadLocalHostInfo : public ThreadLocal::ThreadLocalObject {
    ~ThreadLocalHostInfo() override;
    void updateHostMap(const TlsHostMapSharedPtr& new_host_map);

    TlsHostMapSharedPtr host_map_;
    std::list<LoadDnsCacheEntryHandleImpl*> pending_resolutions_;
  };

  struct DnsHostInfoImpl : public DnsHostInfo {
    DnsHostInfoImpl(TimeSource& time_source, absl::string_view resolved_host, bool is_ip_address)
        : time_source_(time_source), resolved_host_(resolved_host), is_ip_address_(is_ip_address) {
      touch();
    }

    // DnsHostInfo
    Network::Address::InstanceConstSharedPtr address() override { return address_; }
    const std::string& resolvedHost() const override { return resolved_host_; }
    bool isIpAddress() const override { return is_ip_address_; }
    void touch() final { last_used_time_ = time_source_.monotonicTime().time_since_epoch(); }

    TimeSource& time_source_;
    const std::string resolved_host_;
    const bool is_ip_address_;
    bool first_resolve_complete_{};
    Network::Address::InstanceConstSharedPtr address_;
    // Using std::chrono::steady_clock::duration is required for compilation within an atomic vs.
    // using MonotonicTime.
    std::atomic<std::chrono::steady_clock::duration> last_used_time_;
  };

  using DnsHostInfoImplSharedPtr = std::shared_ptr<DnsHostInfoImpl>;

  // Primary host information that accounts for TTL, re-resolution, etc.
  struct PrimaryHostInfo {
    PrimaryHostInfo(DnsCacheImpl& parent, absl::string_view host_to_resolve, uint16_t port,
                    bool is_ip_address, const Event::TimerCb& timer_cb);
    ~PrimaryHostInfo();

    DnsCacheImpl& parent_;
    const uint16_t port_;
    const Event::TimerPtr refresh_timer_;
    const DnsHostInfoImplSharedPtr host_info_;
    Network::ActiveDnsQuery* active_query_{};
  };

  using PrimaryHostInfoPtr = std::unique_ptr<PrimaryHostInfo>;

  struct AddUpdateCallbacksHandleImpl : public AddUpdateCallbacksHandle,
                                        RaiiListElement<AddUpdateCallbacksHandleImpl*> {
    AddUpdateCallbacksHandleImpl(std::list<AddUpdateCallbacksHandleImpl*>& parent,
                                 UpdateCallbacks& callbacks)
        : RaiiListElement<AddUpdateCallbacksHandleImpl*>(parent, this), callbacks_(callbacks) {}

    UpdateCallbacks& callbacks_;
  };

  void startCacheLoad(const std::string& host, uint16_t default_port);
  void startResolve(const std::string& host, PrimaryHostInfo& host_info);
  void finishResolve(const std::string& host, Network::DnsResolver::ResolutionStatus status,
                     std::list<Network::DnsResponse>&& response);
  void runAddUpdateCallbacks(const std::string& host, const DnsHostInfoSharedPtr& host_info);
  void runRemoveCallbacks(const std::string& host);
  void updateTlsHostsMap();
  void onReResolve(const std::string& host);

  Event::Dispatcher& main_thread_dispatcher_;
  const Network::DnsLookupFamily dns_lookup_family_;
  const Network::DnsResolverSharedPtr resolver_;
  const ThreadLocal::SlotPtr tls_slot_;
  Stats::ScopePtr scope_;
  DnsCacheStats stats_;
  std::list<AddUpdateCallbacksHandleImpl*> update_callbacks_;
  absl::flat_hash_map<std::string, PrimaryHostInfoPtr> primary_hosts_;
  DnsCacheResourceManagerImpl resource_manager_;
  const std::chrono::milliseconds refresh_interval_;
  const BackOffStrategyPtr failure_backoff_strategy_;
  const std::chrono::milliseconds host_ttl_;
  const uint32_t max_hosts_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
