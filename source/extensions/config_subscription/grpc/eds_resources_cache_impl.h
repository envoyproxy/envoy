#pragma once

#include <memory>

#include "envoy/config/eds_resources_cache.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Config {

class EdsResourcesCacheImpl : public EdsResourcesCache {
public:
  EdsResourcesCacheImpl(Event::Dispatcher& main_thread_dispatcher)
      : dispatcher_(main_thread_dispatcher) {}

  // EdsResourcesCache
  void setResource(absl::string_view resource_name,
                   const envoy::config::endpoint::v3::ClusterLoadAssignment& resource) override;
  void removeResource(absl::string_view resource_name) override;
  OptRef<const envoy::config::endpoint::v3::ClusterLoadAssignment>
  getResource(absl::string_view resource_name, EdsResourceRemovalCallback* removal_cb) override;
  void removeCallback(absl::string_view resource_name,
                      EdsResourceRemovalCallback* removal_cb) override;
  uint32_t cacheSizeForTest() const override;
  void setExpiryTimer(absl::string_view resource_name, std::chrono::milliseconds ms) override;
  void disableExpiryTimer(absl::string_view resource_name) override;

private:
  // The value of the map, holds the resource and the removal callbacks.
  struct ResourceData {
    envoy::config::endpoint::v3::ClusterLoadAssignment resource_;
    std::vector<EdsResourceRemovalCallback*> removal_cbs_;

    ResourceData(const envoy::config::endpoint::v3::ClusterLoadAssignment& resource)
        : resource_(resource) {}
  };
  // A map between a resource name and its ResourceData.
  absl::flat_hash_map<std::string, ResourceData> resources_map_;
  // The per-resource timeout timer to track when the resource should be removed.
  absl::flat_hash_map<std::string, Event::TimerPtr> expiry_timers_;
  Event::Dispatcher& dispatcher_;
};

} // namespace Config
} // namespace Envoy
