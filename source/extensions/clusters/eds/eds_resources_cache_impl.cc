#include "source/extensions/clusters/eds/eds_resources_cache_impl.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Upstream {

void EdsResourcesCacheImpl::ConfigSourceResourcesMapImpl::setResource(
    absl::string_view resource_name,
    const envoy::config::endpoint::v3::ClusterLoadAssignment& resource) {
  ENVOY_LOG_MISC(trace, "Updating resource {} in the xDS resource cache", resource_name);
  resources_map_.insert_or_assign(resource_name, ResourceData(resource));
}

void EdsResourcesCacheImpl::ConfigSourceResourcesMapImpl::removeResource(
    absl::string_view resource_name) {
  if (const auto& resource_it = resources_map_.find(resource_name);
      resource_it != resources_map_.end()) {
    ENVOY_LOG_MISC(trace, "Removing resource {} from the xDS resource cache", resource_name);
    // Invoke the callbacks, and remove all watchers.
    for (auto& removal_cb : resource_it->second.removal_cbs_) {
      removal_cb->onCachedResourceRemoved(resource_name);
    }
    resource_it->second.removal_cbs_.clear();

    // Remove the resource entry from the cache.
    resources_map_.erase(resource_it);
  }
}

OptRef<const envoy::config::endpoint::v3::ClusterLoadAssignment>
EdsResourcesCacheImpl::ConfigSourceResourcesMapImpl::getResource(
    absl::string_view resource_name, EdsResourceRemovalCallback* removal_cb) {
  if (const auto& resource_it = resources_map_.find(resource_name);
      resource_it != resources_map_.end()) {
    ENVOY_LOG_MISC(trace, "Returning resource {} from the xDS resource cache", resource_name);
    // Add the removal callback to the list associated with the resource.
    if (removal_cb != nullptr) {
      resource_it->second.removal_cbs_.push_back(removal_cb);
    }
    return resource_it->second.resource_;
  }
  // The resource doesn't exist in the resource map.
  return {};
}

void EdsResourcesCacheImpl::ConfigSourceResourcesMapImpl::removeCallback(
    absl::string_view resource_name, EdsResourceRemovalCallback* removal_cb) {
  if (const auto& resource_it = resources_map_.find(resource_name);
      resource_it != resources_map_.end()) {
    ENVOY_LOG_MISC(trace, "Removing callback for resource {} from the xDS resource cache",
                   resource_name);
    resource_it->second.removal_cbs_.remove(removal_cb);
  }
}

EdsResourcesCache::ConfigSourceResourcesMap& EdsResourcesCacheImpl::getConfigSourceResourceMap(
    const envoy::config::core::v3::ConfigSource& config_source) {
  // Fetch the resource map for the config source. Create it if it doesn't exist.
  auto it = config_source_map_.find(config_source);
  if (it == config_source_map_.end()) {
    it = config_source_map_.emplace(config_source, std::make_unique<ConfigSourceResourcesMapImpl>())
             .first;
  }
  return *(it->second.get());
}

} // namespace Upstream
} // namespace Envoy
