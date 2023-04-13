#pragma once

#include "envoy/upstream/eds_resources_cache.h"

#include "source/common/protobuf/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

// This is an xDS resources cache for EDS subscriptions. It is a multi-level cache,
// where the first level is a per-ConfigSource cache, and the second level is
// per resource-name cache. In addition to saving resources in the cache,
// retrieving them, and removing them, the cache allows attaching watchers to the
// retrieved resources using ResourceRemovalCallback. The watchers will be notified when
// the resource is removed from the cache.
class EdsResourcesCacheImpl : public EdsResourcesCache {
public:
  // A resource-name to resource data map for a specific config source.
  class ConfigSourceResourcesMapImpl : public EdsResourcesCache::ConfigSourceResourcesMap {
  public:
    void setResource(absl::string_view resource_name,
                     const envoy::config::endpoint::v3::ClusterLoadAssignment& resource) override;

    void removeResource(absl::string_view resource_name) override;

    OptRef<const envoy::config::endpoint::v3::ClusterLoadAssignment>
    getResource(absl::string_view resource_name, EdsResourceRemovalCallback* removal_cb) override;

    void removeCallback(absl::string_view resource_name,
                        EdsResourceRemovalCallback* removal_cb) override;

    uint32_t cacheSizeForTest() const override { return resources_map_.size(); }

  private:
    // The value of the map, holds the resource and the removal callbacks.
    struct ResourceData {
      envoy::config::endpoint::v3::ClusterLoadAssignment resource_;
      std::list<EdsResourceRemovalCallback*> removal_cbs_;

      ResourceData(const envoy::config::endpoint::v3::ClusterLoadAssignment& resource) {
        resource_.CopyFrom(resource);
      }
    };
    // A map between a resource name and its ResourceData.
    absl::flat_hash_map<std::string, ResourceData> resources_map_;
  };

  // The first level cache for a given ConfigSource.
  ConfigSourceResourcesMap&
  getConfigSourceResourceMap(const envoy::config::core::v3::ConfigSource& config_source) override;

private:
  using ConfigSourceResourcesMapPtr = std::unique_ptr<ConfigSourceResourcesMap>;

  // A type for the first-level cache that maps a ConfigSource to the
  // second-level cache.
  using ConfigSourceMap =
      absl::flat_hash_map<envoy::config::core::v3::ConfigSource, ConfigSourceResourcesMapPtr,
                          MessageUtil, MessageUtil>;

  // The first-level cache, maps a ConfigSource to the second-level cache.
  ConfigSourceMap config_source_map_;
};

} // namespace Upstream
} // namespace Envoy
