#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/resource_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

/**
 * A cached DNS host.
 */
class DnsHostInfo {
public:
  virtual ~DnsHostInfo() = default;

  /**
   * Returns the host's currently resolved address. This address may change periodically due to
   * async re-resolution.
   */
  virtual Network::Address::InstanceConstSharedPtr address() const PURE;

  /**
   * Returns the host that was actually resolved via DNS. If port was originally specified it will
   * be stripped from this return value.
   */
  virtual const std::string& resolvedHost() const PURE;

  /**
   * Returns whether the original host is an IP address.
   */
  virtual bool isIpAddress() const PURE;

  /**
   * Indicates that the host has been used and should not be purged depending on any configured
   * TTL policy
   */
  virtual void touch() PURE;
};

using DnsHostInfoSharedPtr = std::shared_ptr<DnsHostInfo>;

#define ALL_DNS_CACHE_CIRCUIT_BREAKERS_STATS(OPEN_GAUGE, REMAINING_GAUGE)                          \
  OPEN_GAUGE(rq_pending_open, Accumulate)                                                          \
  REMAINING_GAUGE(rq_pending_remaining, Accumulate)

struct DnsCacheCircuitBreakersStats {
  ALL_DNS_CACHE_CIRCUIT_BREAKERS_STATS(GENERATE_GAUGE_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * A resource manager of DNS Cache.
 */
class DnsCacheResourceManager {
public:
  virtual ~DnsCacheResourceManager() = default;

  /**
   * Returns the resource limit of pending requests to DNS.
   */
  virtual ResourceLimit& pendingRequests() PURE;

  /**
   * Returns the reference of stats for dns cache circuit breakers.
   */
  virtual DnsCacheCircuitBreakersStats& stats() PURE;
};

/**
 * A cache of DNS hosts. Hosts will re-resolve their addresses or be automatically purged
 * depending on configured policy.
 */
class DnsCache {
public:
  /**
   * Callbacks used in the loadDnsCacheEntry() method.
   */
  class LoadDnsCacheEntryCallbacks {
  public:
    virtual ~LoadDnsCacheEntryCallbacks() = default;

    /**
     * Called when the DNS cache load is complete (or failed).
     *
     * @param host_info the DnsHostInfo for the resolved host.
     */
    virtual void onLoadDnsCacheComplete(const DnsHostInfoSharedPtr& host_info) PURE;
  };

  /**
   * Handle returned from loadDnsCacheEntry(). Destruction of the handle will cancel any future
   * callback.
   */
  class LoadDnsCacheEntryHandle {
  public:
    virtual ~LoadDnsCacheEntryHandle() = default;
  };

  using LoadDnsCacheEntryHandlePtr = std::unique_ptr<LoadDnsCacheEntryHandle>;

  /**
   * Update callbacks that can be registered in the addUpdateCallbacks() method.
   */
  class UpdateCallbacks {
  public:
    virtual ~UpdateCallbacks() = default;

    /**
     * Called when a host has been added or has had its address updated.
     * @param host supplies the added/updated host.
     * @param host_info supplies the associated host info.
     */
    virtual void onDnsHostAddOrUpdate(const std::string& host,
                                      const DnsHostInfoSharedPtr& host_info) PURE;

    /**
     * Called when a host has been removed.
     * @param host supplies the removed host.
     */
    virtual void onDnsHostRemove(const std::string& host) PURE;
  };

  /**
   * Handle returned from addUpdateCallbacks(). Destruction of the handle will remove the
   * registered callbacks.
   */
  class AddUpdateCallbacksHandle {
  public:
    virtual ~AddUpdateCallbacksHandle() = default;
  };

  using AddUpdateCallbacksHandlePtr = std::unique_ptr<AddUpdateCallbacksHandle>;

  virtual ~DnsCache() = default;

  /**
   * Initiate a DNS cache load.
   * @param host supplies the host to load. Hosts are cached inclusive of port, even though the
   *             port will be stripped during resolution. This means that 'a.b.c' and 'a.b.c:9001'
   *             will both resolve 'a.b.c' but will generate different host entries with different
   *             target ports.
   * @param default_port supplies the port to use if the host does not have a port embedded in it.
   * @param callbacks supplies the cache load callbacks to invoke if async processing is needed.
   * @return a cache load result which includes both a status and handle. If the handle is non-null
   *         the callbacks will be invoked at a later time, otherwise consult the status for the
   *         reason the cache is not loading. In this case, callbacks will never be called.
   */
  enum class LoadDnsCacheEntryStatus {
    // The cache entry is already loaded. Callbacks will not be called.
    InCache,
    // The cache entry is loading. Callbacks will be called at a later time unless cancelled.
    Loading,
    // The cache is full and the requested host is not in cache. Callbacks will not be called.
    Overflow
  };

  struct LoadDnsCacheEntryResult {
    LoadDnsCacheEntryStatus status_;
    LoadDnsCacheEntryHandlePtr handle_;
    absl::optional<DnsHostInfoSharedPtr> host_info_;
  };

  virtual LoadDnsCacheEntryResult loadDnsCacheEntry(absl::string_view host, uint16_t default_port,
                                                    LoadDnsCacheEntryCallbacks& callbacks) PURE;

  /**
   * Add update callbacks to the cache.
   * @param callbacks supplies the callbacks to add.
   * @return a handle that on destruction will de-register the callbacks.
   */
  virtual AddUpdateCallbacksHandlePtr addUpdateCallbacks(UpdateCallbacks& callbacks) PURE;

  using IterateHostMapCb = std::function<void(absl::string_view, const DnsHostInfoSharedPtr&)>;

  /**
   * Iterates over all entries in the cache, calling a callback for each entry
   *
   * @param iterate_callback the callback to invoke for each entry in the cache
   */
  virtual void iterateHostMap(IterateHostMapCb iterate_callback) PURE;

  /**
   * Retrieve the DNS host info of a given host currently stored in the cache.
   * @param host_name supplies the host name.
   * @return the DNS host info associated with the given host name if the host's address is cached,
   * otherwise `absl::nullopt`.
   */
  virtual absl::optional<const DnsHostInfoSharedPtr> getHost(absl::string_view host_name) PURE;

  /**
   * Check if a DNS request is allowed given resource limits.
   * @return RAII handle for pending request circuit breaker if the request was allowed.
   */
  virtual Upstream::ResourceAutoIncDecPtr canCreateDnsRequest() PURE;
};

using DnsCacheSharedPtr = std::shared_ptr<DnsCache>;

/**
 * A manager for multiple DNS caches.
 */
class DnsCacheManager {
public:
  virtual ~DnsCacheManager() = default;

  /**
   * Get a DNS cache.
   * @param config supplies the cache parameters. If a cache exists with the same parameters it
   *               will be returned, otherwise a new one will be created.
   */
  virtual DnsCacheSharedPtr
  getCache(const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) PURE;
};

using DnsCacheManagerSharedPtr = std::shared_ptr<DnsCacheManager>;

/**
 * Factory for getting a DNS cache manager.
 */
class DnsCacheManagerFactory {
public:
  virtual ~DnsCacheManagerFactory() = default;

  /**
   * Get a DNS cache manager.
   */
  virtual DnsCacheManagerSharedPtr get() PURE;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
