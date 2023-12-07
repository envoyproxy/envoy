#pragma once

#include "envoy/common/backoff_strategy.h"
#include "envoy/common/key_value_store.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/http/filter.h"
#include "envoy/network/dns.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/cleanup.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_resource_manager.h"
#include "source/server/generic_factory_context.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

/**
 * All DNS cache stats. @see stats_macros.h
 */
#define ALL_DNS_CACHE_STATS(COUNTER, GAUGE)                                                        \
  COUNTER(cache_load)                                                                              \
  COUNTER(dns_query_attempt)                                                                       \
  COUNTER(dns_query_failure)                                                                       \
  COUNTER(dns_query_success)                                                                       \
  COUNTER(dns_query_timeout)                                                                       \
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

class DnsCacheImplTest;

class DnsCacheImpl : public DnsCache, Logger::Loggable<Logger::Id::forward_proxy> {
public:
  // Create a DnsCacheImpl or return a failed status;
  static absl::StatusOr<std::shared_ptr<DnsCacheImpl>> createDnsCacheImpl(
      const Server::Configuration::GenericFactoryContext& context,
      const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config);

  ~DnsCacheImpl() override;
  static DnsCacheStats generateDnsCacheStats(Stats::Scope& scope);
  static Network::DnsResolverSharedPtr selectDnsResolver(
      const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config,
      Event::Dispatcher& main_thread_dispatcher,
      Server::Configuration::FactoryContextBase& context);

  // DnsCache
  LoadDnsCacheEntryResult loadDnsCacheEntry(absl::string_view host, uint16_t default_port,
                                            bool is_proxy_lookup,
                                            LoadDnsCacheEntryCallbacks& callbacks) override;
  AddUpdateCallbacksHandlePtr addUpdateCallbacks(UpdateCallbacks& callbacks) override;
  void iterateHostMap(IterateHostMapCb cb) override;
  absl::optional<const DnsHostInfoSharedPtr> getHost(absl::string_view host_name) override;
  Upstream::ResourceAutoIncDecPtr canCreateDnsRequest() override;
  void forceRefreshHosts() override;

private:
  DnsCacheImpl(const Server::Configuration::GenericFactoryContext& context,
               const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config);
  struct LoadDnsCacheEntryHandleImpl
      : public LoadDnsCacheEntryHandle,
        RaiiMapOfListElement<std::string, LoadDnsCacheEntryHandleImpl*> {
    LoadDnsCacheEntryHandleImpl(
        absl::flat_hash_map<std::string, std::list<LoadDnsCacheEntryHandleImpl*>>& parent,
        absl::string_view host, LoadDnsCacheEntryCallbacks& callbacks)
        : RaiiMapOfListElement<std::string, LoadDnsCacheEntryHandleImpl*>(parent, host, this),
          callbacks_(callbacks) {}

    LoadDnsCacheEntryCallbacks& callbacks_;
  };

  class DnsHostInfoImpl;
  using DnsHostInfoImplSharedPtr = std::shared_ptr<DnsHostInfoImpl>;

  struct HostMapUpdateInfo {
    HostMapUpdateInfo(const std::string& host, DnsHostInfoImplSharedPtr info)
        : host_(host), info_(std::move(info)) {}
    std::string host_;
    DnsHostInfoImplSharedPtr info_;
  };
  using HostMapUpdateInfoSharedPtr = std::shared_ptr<HostMapUpdateInfo>;

  // Per-thread DNS cache info including pending callbacks.
  struct ThreadLocalHostInfo : public ThreadLocal::ThreadLocalObject {
    ThreadLocalHostInfo(DnsCacheImpl& parent) : parent_{parent} {}
    ~ThreadLocalHostInfo() override;
    void onHostMapUpdate(const HostMapUpdateInfoSharedPtr& resolved_info);
    absl::flat_hash_map<std::string, std::list<LoadDnsCacheEntryHandleImpl*>> pending_resolutions_;
    DnsCacheImpl& parent_;
  };

  class DnsHostInfoImpl : public DnsHostInfo {
  public:
    DnsHostInfoImpl(TimeSource& time_source, absl::string_view resolved_host, bool is_ip_address)
        : time_source_(time_source), resolved_host_(resolved_host), is_ip_address_(is_ip_address),
          stale_at_time_(time_source.monotonicTime()) {
      touch();
    }

    // DnsHostInfo
    Network::Address::InstanceConstSharedPtr address() const override {
      absl::ReaderMutexLock lock{&resolve_lock_};
      return address_;
    }

    std::vector<Network::Address::InstanceConstSharedPtr> addressList() const override {
      std::vector<Network::Address::InstanceConstSharedPtr> ret;
      absl::ReaderMutexLock lock{&resolve_lock_};
      ret = address_list_;
      return ret;
    }

    const std::string& resolvedHost() const override { return resolved_host_; }
    bool isIpAddress() const override { return is_ip_address_; }
    void touch() final { last_used_time_ = time_source_.monotonicTime().time_since_epoch(); }
    void updateStale(MonotonicTime resolution_time, std::chrono::seconds ttl) {
      stale_at_time_ = resolution_time + ttl;
    }
    bool isStale() {
      return time_source_.monotonicTime() > static_cast<MonotonicTime>(stale_at_time_);
    }

    void setAddresses(Network::Address::InstanceConstSharedPtr address,
                      std::vector<Network::Address::InstanceConstSharedPtr>&& list) {
      absl::WriterMutexLock lock{&resolve_lock_};
      if (!(Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.dns_cache_set_first_resolve_complete"))) {
        first_resolve_complete_ = true;
      }
      address_ = address;
      address_list_ = std::move(list);
    }

    std::chrono::steady_clock::duration lastUsedTime() const { return last_used_time_.load(); }

    bool firstResolveComplete() const override {
      absl::ReaderMutexLock lock{&resolve_lock_};
      return first_resolve_complete_;
    }

    void setFirstResolveComplete() {
      absl::WriterMutexLock lock{&resolve_lock_};
      first_resolve_complete_ = true;
    }

  private:
    friend class DnsCacheImplTest;
    TimeSource& time_source_;
    const std::string resolved_host_;
    const bool is_ip_address_;
    mutable absl::Mutex resolve_lock_;
    Network::Address::InstanceConstSharedPtr address_ ABSL_GUARDED_BY(resolve_lock_);
    std::vector<Network::Address::InstanceConstSharedPtr>
        address_list_ ABSL_GUARDED_BY(resolve_lock_);

    // Using std::chrono::steady_clock::duration is required for compilation within an atomic vs.
    // using MonotonicTime.
    std::atomic<std::chrono::steady_clock::duration> last_used_time_;
    std::atomic<MonotonicTime> stale_at_time_;
    bool first_resolve_complete_ ABSL_GUARDED_BY(resolve_lock_){false};
  };

  // Primary host information that accounts for TTL, re-resolution, etc.
  struct PrimaryHostInfo {
    PrimaryHostInfo(DnsCacheImpl& parent, absl::string_view host_to_resolve, uint16_t port,
                    bool is_ip_address, const Event::TimerCb& refresh_timer_cb,
                    const Event::TimerCb& timeout_timer_cb);
    ~PrimaryHostInfo();

    DnsCacheImpl& parent_;
    const uint16_t port_;
    const Event::TimerPtr refresh_timer_;
    const Event::TimerPtr timeout_timer_;
    const DnsHostInfoImplSharedPtr host_info_;
    const BackOffStrategyPtr failure_backoff_strategy_;
    Network::ActiveDnsQuery* active_query_{};
  };

  // Hold PrimaryHostInfo by shared_ptr to avoid having to hold the map mutex while updating
  // individual entries.
  using PrimaryHostInfoPtr = std::unique_ptr<PrimaryHostInfo>;

  struct AddUpdateCallbacksHandleImpl : public AddUpdateCallbacksHandle,
                                        RaiiListElement<AddUpdateCallbacksHandleImpl*> {
    AddUpdateCallbacksHandleImpl(std::list<AddUpdateCallbacksHandleImpl*>& parent,
                                 UpdateCallbacks& callbacks)
        : RaiiListElement<AddUpdateCallbacksHandleImpl*>(parent, this), callbacks_(callbacks) {}

    UpdateCallbacks& callbacks_;
  };

  void startCacheLoad(const std::string& host, uint16_t default_port, bool is_proxy_lookup);

  void startResolve(const std::string& host, PrimaryHostInfo& host_info)
      ABSL_LOCKS_EXCLUDED(primary_hosts_lock_);

  void finishResolve(const std::string& host, Network::DnsResolver::ResolutionStatus status,
                     std::list<Network::DnsResponse>&& response,
                     absl::optional<MonotonicTime> resolution_time = {},
                     bool is_proxy_lookup = false);
  void runAddUpdateCallbacks(const std::string& host, const DnsHostInfoSharedPtr& host_info);
  void runResolutionCompleteCallbacks(const std::string& host,
                                      const DnsHostInfoSharedPtr& host_info,
                                      Network::DnsResolver::ResolutionStatus status);
  void runRemoveCallbacks(const std::string& host);
  void notifyThreads(const std::string& host, const DnsHostInfoImplSharedPtr& resolved_info);
  void onReResolve(const std::string& host);
  void onResolveTimeout(const std::string& host);
  PrimaryHostInfo& getPrimaryHost(const std::string& host);

  void addCacheEntry(const std::string& host,
                     const Network::Address::InstanceConstSharedPtr& address,
                     const std::vector<Network::Address::InstanceConstSharedPtr>& address_list,
                     const std::chrono::seconds ttl);
  void removeCacheEntry(const std::string& host);
  void loadCacheEntries(
      const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config);
  PrimaryHostInfo* createHost(const std::string& host, uint16_t default_port);
  absl::optional<Network::DnsResponse> parseValue(absl::string_view value,
                                                  absl::optional<MonotonicTime>& resolution_time);

  Event::Dispatcher& main_thread_dispatcher_;
  const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config_;
  Random::RandomGenerator& random_generator_;
  const Network::DnsLookupFamily dns_lookup_family_;
  const Network::DnsResolverSharedPtr resolver_;
  ThreadLocal::TypedSlot<ThreadLocalHostInfo> tls_slot_;
  Stats::ScopeSharedPtr scope_;
  DnsCacheStats stats_;
  std::list<AddUpdateCallbacksHandleImpl*> update_callbacks_;
  absl::Mutex primary_hosts_lock_;
  absl::flat_hash_map<std::string, PrimaryHostInfoPtr>
      primary_hosts_ ABSL_GUARDED_BY(primary_hosts_lock_);
  std::unique_ptr<KeyValueStore> key_value_store_;
  DnsCacheResourceManagerImpl resource_manager_;
  const std::chrono::milliseconds refresh_interval_;
  const std::chrono::milliseconds min_refresh_interval_;
  const std::chrono::milliseconds timeout_interval_;
  Filesystem::Instance& file_system_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  const std::chrono::milliseconds host_ttl_;
  const uint32_t max_hosts_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
