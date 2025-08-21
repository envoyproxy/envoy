#include "source/extensions/config_subscription/grpc/eds_resources_cache_impl.h"

#include <algorithm>

#include "source/common/common/logger.h"

namespace Envoy {
namespace Config {

void EdsResourcesCacheImpl::setResource(
    absl::string_view resource_name,
    const envoy::config::endpoint::v3::ClusterLoadAssignment& resource) {
  resources_map_.insert_or_assign(resource_name, ResourceData(resource));
}

void EdsResourcesCacheImpl::removeResource(absl::string_view resource_name) {
  if (const auto& resource_it = resources_map_.find(resource_name);
      resource_it != resources_map_.end()) {
    // Invoke the callbacks, and remove all watchers.
    for (auto& removal_cb : resource_it->second.removal_cbs_) {
      removal_cb->onCachedResourceRemoved(resource_name);
    }
    resource_it->second.removal_cbs_.clear();

    // Remove the resource entry from the cache.
    resources_map_.erase(resource_it);
  }
  // Remove the expiration timers (if any) as there's no longer interest in the resource.
  expiry_timers_.erase(resource_name);
}

OptRef<const envoy::config::endpoint::v3::ClusterLoadAssignment>
EdsResourcesCacheImpl::getResource(absl::string_view resource_name,
                                   EdsResourceRemovalCallback* removal_cb) {
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

void EdsResourcesCacheImpl::removeCallback(absl::string_view resource_name,
                                           EdsResourceRemovalCallback* removal_cb) {
  if (const auto& resource_it = resources_map_.find(resource_name);
      resource_it != resources_map_.end()) {
    ENVOY_LOG_MISC(trace, "Removing callback for resource {} from the xDS resource cache",
                   resource_name);
    auto& callbacks = resource_it->second.removal_cbs_;
    // Using the Erase-Remove idiom, in which all entries to be removed are
    // moved to the end of the vector by the remove() call, and then the erase() call
    // will erase these entries from the first element to be removed all the
    // way to the end of the vector.
    callbacks.erase(std::remove(callbacks.begin(), callbacks.end(), removal_cb), callbacks.end());
  }
}

uint32_t EdsResourcesCacheImpl::cacheSizeForTest() const { return resources_map_.size(); }

void EdsResourcesCacheImpl::setExpiryTimer(absl::string_view resource_name,
                                           std::chrono::milliseconds ms) {
  auto it = expiry_timers_.find(resource_name);
  if (it == expiry_timers_.end()) {
    // No timer for this resource, create one, and create a copy of resource_name that will outlive
    // this function.
    Event::TimerPtr resource_timeout =
        dispatcher_.createTimer([this, str_resource_name = std::string(resource_name)]() -> void {
          // On expiration the resource is removed (from the cache and from the watchers).
          removeResource(str_resource_name);
        });
    it = expiry_timers_.emplace(resource_name, std::move(resource_timeout)).first;
  }
  (it->second)->enableTimer(ms);
}

void EdsResourcesCacheImpl::disableExpiryTimer(absl::string_view resource_name) {
  auto it = expiry_timers_.find(resource_name);
  if (it != expiry_timers_.end()) {
    (it->second)->disableTimer();
    // Remove the timer as it is no longer needed.
    expiry_timers_.erase(it);
  }
}

} // namespace Config
} // namespace Envoy
