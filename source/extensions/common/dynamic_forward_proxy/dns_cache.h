#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/resource_manager.h"

#include "source/common/http/header_utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

/**
 * A cached DNS host.
 */
class DnsHostInfo {
public:
  // DFP hosts are created after a DNS lookup for a domain name (e.g. foo.com) but are created
  // with an IP address and port to resolve to. To prevent bugs where we insert say foo.com
  // (default port 80) then later look up foo.com (default port 443) and get IP addresses with
  // port 80, we "fully qualify" hostnames with the port.
  //
  // This normalizes hostnames, respecting the port if it exists, and adding the default port
  // if there is no port.
  static std::string normalizeHostForDfp(absl::string_view host, uint16_t default_port) {
    if (Envoy::Http::HeaderUtility::hostHasPort(host)) {
      return std::string(host);
    }
    return absl::StrCat(host, ":", default_port);
  }

  virtual ~DnsHostInfo() = default;

  /**
   * Returns the host's currently resolved address. This address may change periodically due to
   * async re-resolution. This address may be null in the case of failed resolution.
   */
  virtual Network::Address::InstanceConstSharedPtr address() const PURE;

  /**
   * Returns whether the first DNS resolving attempt is completed or not.
   */
  virtual bool firstResolveComplete() const PURE;

  /**
   * Returns the host's currently resolved address. These addresses may change periodically due to
   * async re-resolution.
   */
  virtual std::vector<Network::Address::InstanceConstSharedPtr> addressList() const PURE;

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

  /**
   * Returns details about the resolution which resulted in the addresses above.
   * This includes both success and failure details.
   */
  virtual std::string details() PURE;
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

    /**
     * Called when any resolution for a host completes.
     * @param host supplies the added/updated host.
     * @param host_info supplies the associated host info.
     * @param status supplies the resolution status.
     */
    virtual void onDnsResolutionComplete(const std::string& host,
                                         const DnsHostInfoSharedPtr& host_info,
                                         Network::DnsResolver::ResolutionStatus status) PURE;
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

  /**
   * Legacy API to avoid churn while we determine if |force_refresh| below is useful.
   */
  virtual LoadDnsCacheEntryResult loadDnsCacheEntry(absl::string_view host, uint16_t default_port,
                                                    bool is_proxy_lookup,
                                                    LoadDnsCacheEntryCallbacks& callbacks) {
    return loadDnsCacheEntryWithForceRefresh(host, default_port, is_proxy_lookup, false, callbacks);
  }

  /**
   * Attempt to load a DNS cache entry.
   * @param host the hostname to lookup
   * @param default_port the port to use
   * @param is_proxy_lookup indicates if the request is safe to fast-fail. The Dynamic Forward Proxy
   * filter sets this to true if no address is necessary due to an upstream proxy being configured.
   * @param force_refresh forces a fresh DNS cache lookup if true.
   * @return a handle that on destruction will de-register the callbacks.
   */
  virtual LoadDnsCacheEntryResult
  loadDnsCacheEntryWithForceRefresh(absl::string_view host, uint16_t default_port,
                                    bool is_proxy_lookup, bool force_refresh,
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

  /**
   * Force a DNS refresh of all known hosts, ignoring any ongoing failure or success timers. This
   * can be used in response to network changes which might alter DNS responses, for example.
   */
  virtual void forceRefreshHosts() PURE;

  /**
   * Sets the `IpVersion` addresses to be removed from the DNS response. This can be useful for a
   * use case where the DNS response returns both IPv4 and IPv6 and we are only interested a
   * specific IP version, we can save time not having to try to connect to both IPv4 and IPv6
   * addresses.
   */
  virtual void setIpVersionToRemove(absl::optional<Network::Address::IpVersion> ip_version) PURE;

  /**
   * Stops the DNS cache background tasks by canceling the pending queries and stopping the timeout
   * and refresh timers. This function can be useful when the network is unavailable, such as when
   * a device is in airplane mode, etc.
   */
  virtual void stop() PURE;
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
  virtual absl::StatusOr<DnsCacheSharedPtr>
  getCache(const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) PURE;

  /**
   * Look up an existing DNS cache by name.
   * @param name supplies the cache name to look up. If a cache exists with the same name it
   *             will be returned.
   * @return pointer to the cache if it exists, nullptr otherwise.
   */
  virtual DnsCacheSharedPtr lookUpCacheByName(absl::string_view cache_name) PURE;
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
