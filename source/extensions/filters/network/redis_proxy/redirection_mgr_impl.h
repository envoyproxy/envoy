#pragma once

#include <array>
#include <atomic>
#include <numeric>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/filters/network/redis_proxy/redirection_mgr.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class RedirectionManagerImpl : public RedirectionManager,
                               public Envoy::Singleton::Instance,
                               public std::enable_shared_from_this<RedirectionManagerImpl> {
public:
  friend class RedirectionMgrTest;

  /**
   * The information that the manager keeps for each cluster upon registration.
   */
  struct ClusterInfo {
    ClusterInfo(std::chrono::milliseconds min_time_between_triggering, uint32_t redirects_threshold,
                RedirectCB cb)
        : min_time_between_triggering_(min_time_between_triggering),
          redirects_threshold_(redirects_threshold), cb_(std::move(cb)) {}
    std::atomic<uint64_t> last_callback_time_ms_{};
    std::atomic<uint32_t> redirects_count_{};
    std::chrono::milliseconds min_time_between_triggering_;
    uint32_t redirects_threshold_;
    RedirectCB cb_;
  };

  using ClusterInfoSharedPtr = std::shared_ptr<ClusterInfo>;

  class HandleImpl : public Handle {
  public:
    HandleImpl(const std::string& cluster_name, RedirectionManagerImpl* mgr)
        : manager_(mgr->shared_from_this()), cluster_name_(cluster_name) {}

    ~HandleImpl() override {
      std::shared_ptr<RedirectionManagerImpl> strong_manager = manager_.lock();
      if (strong_manager) {
        strong_manager->unregisterCluster(cluster_name_);
      }
    }

  private:
    std::weak_ptr<RedirectionManagerImpl> manager_;
    std::string cluster_name_;
  };

  RedirectionManagerImpl(Event::Dispatcher& main_thread_dispatcher, Upstream::ClusterManager& cm,
                         TimeSource& time_source)
      : main_thread_dispatcher_(main_thread_dispatcher), cm_(cm), time_source_(time_source) {}

  bool onRedirection(const std::string& cluster_name) override;

  HandlePtr registerCluster(const std::string& cluster_name,
                            const std::chrono::milliseconds min_time_between_triggering,
                            const uint32_t redirects_threshold, const RedirectCB cb) override;

  void unregisterCluster(const std::string& cluster_name) override;

private:
  Event::Dispatcher& main_thread_dispatcher_;
  Upstream::ClusterManager& cm_;
  TimeSource& time_source_;
  std::map<std::string, ClusterInfoSharedPtr> info_map_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
