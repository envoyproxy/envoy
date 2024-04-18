#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

// An interface for cached resource removed callback.
class EdsResourceRemovalCallback {
public:
  virtual ~EdsResourceRemovalCallback() = default;

  // Invoked when a cached resource is removed from the cache.
  virtual void onCachedResourceRemoved(absl::string_view resource_name) PURE;
};

// Represents an xDS resources cache for EDS resources, and currently supports
// a single config-source (ADS). The motivation is that clusters that are
// updated (not added) during a CDS response will be able to use the current EDS
// configuration, thus avoiding the need of the xDS server to send an additional
// EDS response that is identical to what was already sent.
// However, using the cached EDS config is not always desired, for example when
// the cluster changes from non-TLS to TLS (see discussion in
// https://github.com/envoyproxy/envoy/issues/5168). Thus, this cache allows
// the EDS subscription to decide whether to use the cache or not.
//
// This cache will be instantiated once and owned by the ADS Mux, and passed to
// the EDS subscriptions.
//
// Resources lifetime in the cache is determined by the gRPC mux that adds/updates a
// resource when it receives its contents, and removes a resource when there is
// no longer interest in that resource.
// An EDS subscription may fetch a resource from the cache, and optionally
// install a callback to be triggered if the resource is removed from the cache.
// In addition, a resource in the cache may have an expiration timer if
// "endpoint_stale_after" (TTL) is set for that resource. Once the timer
// expires, the callbacks will be triggered to remove the resource.
class EdsResourcesCache {
public:
  virtual ~EdsResourcesCache() = default;

  /**
   * Adds or updates a given resource name with its resource.
   * Any callback that was previously assigned to the resource will be removed
   * without any notification.
   * @param resource_name the name of the resource to add/update.
   * @param resource the contents of the resource.
   */
  virtual void setResource(absl::string_view resource_name,
                           const envoy::config::endpoint::v3::ClusterLoadAssignment& resource) PURE;

  /**
   * Removes a resource from the resource cache given the resource name.
   * The callbacks for the resource will be invoked, notifying that the resource
   * is removed.
   * @param resource_name the name of the resource that will be removed from
   *        the cache.
   */
  virtual void removeResource(absl::string_view resource_name) PURE;

  /**
   * Retrieves a resource from the cache, and adds the given callback (if any)
   * to the resource's removal list. if the resource is removed, all callbacks
   * for that resource will be invoked.
   * @param resource_name the name of the resource to fetch.
   * @param removal_cb an optional callback that will be invoked if the resource is removed
   *        in the future. Note that updating the resource (`setResource()`) will also
   *        remove the callback. The caller of this function can also call
   *        `removeCallback()` to explicitly remove the callback. The callback
   *        is owned by the caller as it is part of the EDS subscription.
   * @return A reference to the cluster load assignment resource, or nullopt if the
   *         resource doesn't exist.
   */
  virtual OptRef<const envoy::config::endpoint::v3::ClusterLoadAssignment>
  getResource(absl::string_view resource_name, EdsResourceRemovalCallback* removal_cb) PURE;

  /**
   * Removes a callback for a given resource name (if it was previously added).
   * @param resource_name the name of the resource for which the callback should be removed.
   * @param removal_cb a pointer to the callback that needs to be removed.
   */
  virtual void removeCallback(absl::string_view resource_name,
                              EdsResourceRemovalCallback* removal_cb) PURE;

  /**
   * Sets an expiry timer for the given resource_name after the given ms milliseconds.
   * Once the timer expires, the callbacks for that resource (if any) will be
   * @param resource_name the name of the resource for which the timer should be added.
   * @param ms the number of milliseconds until expiration.
   */
  virtual void setExpiryTimer(absl::string_view resource_name, std::chrono::milliseconds ms) PURE;

  /**
   * Disables the expiration timer for the given resource_name.
   * @param resource_name the name of the resource for which the timer should be disabled.
   */
  virtual void disableExpiryTimer(absl::string_view resource_name) PURE;

  /**
   * Returns the number of items in the cache. Only used in tests.
   * @return the number of items in the cache.
   */
  virtual uint32_t cacheSizeForTest() const PURE;
};

using EdsResourcesCachePtr = std::unique_ptr<EdsResourcesCache>;
using EdsResourcesCacheOptRef = OptRef<EdsResourcesCache>;

} // namespace Config
} // namespace Envoy
