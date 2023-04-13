#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

// An interface for cached resource removed callback.
class EdsResourceRemovalCallback {
public:
  virtual ~EdsResourceRemovalCallback() = default;

  // Invoked when a cached resource is removed from the cache.
  virtual void onCachedResourceRemoved(absl::string_view resource_name) PURE;
};

// This is an xDS resources cache for EDS subscriptions. It is a multi-level cache,
// where the first level is a per-ConfigSource cache, and the second level is
// per resource-name cache. In addition to saving resources in the cache,
// retrieving them, and removing them, the cache allows attaching watchers to the
// retrieved resources using ResourceRemovalCallback. The watchers will be notified when
// the resource is removed from the cache.
// This cache is needed to allow EDS subscriptions to use previously fetched
// EDS resources, but only if no new EDS resource arrives. The removal callback
// is used to ensure resource TTL is working properly.
// TODO(adisuissa): In the future this can be a per-xDS resource-type cache.
class EdsResourcesCache {
public:
  virtual ~EdsResourcesCache() = default;

  class ConfigSourceResourcesMap {
  public:
    virtual ~ConfigSourceResourcesMap() = default;

    /**
     * Adds or updates a given resource name with its resource.
     * Any watcher that was previously assigned to the resource will be removed
     * without any notification.
     * @param resource_name the name of the resource to add/update.
     * @param resource the contents of the resource.
     */
    virtual void
    setResource(absl::string_view resource_name,
                const envoy::config::endpoint::v3::ClusterLoadAssignment& resource) PURE;

    /**
     * Removes a resource from the resource cache given the resource name.
     * The watchers for the resource will be notified that the resource is
     * removed.
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
     *        in the future. Note that setting the resource will also remove the callback.
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
     * Returns the number of items in the cache. Only used in tests.
     * @return the number of items in the cache.
     */
    virtual uint32_t cacheSizeForTest() const PURE;
  };

  /**
   * Returns a resource map for the given config source (creates if it doesn't exist).
   * @param config_source the config source for which a resources map will be returned.
   * @return a resource map for the config source.
   */
  virtual ConfigSourceResourcesMap&
  getConfigSourceResourceMap(const envoy::config::core::v3::ConfigSource& config_source) PURE;
};

} // namespace Upstream
} // namespace Envoy
