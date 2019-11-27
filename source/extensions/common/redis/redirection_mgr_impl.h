#pragma once

#include <array>
#include <atomic>
#include <numeric>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"

#include "extensions/common/redis/redirection_mgr.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

class RedirectionManagerImpl : public RedirectionManager,
                               public Envoy::Singleton::Instance,
                               public std::enable_shared_from_this<RedirectionManagerImpl> {
public:
  friend class RedirectionMgrTest;

  /**
   * The information that the manager keeps for each cluster upon registration.
   */
  struct ClusterInfo {
    ClusterInfo(std::string cluster_name, std::chrono::milliseconds min_time_between_triggering,
                uint32_t redirects_threshold, RedirectCB cb)
        : cluster_name_(std::move(cluster_name)),
          min_time_between_triggering_(min_time_between_triggering),
          redirects_threshold_(redirects_threshold), cb_(std::move(cb)) {}
    std::string cluster_name_;
    std::atomic<uint64_t> last_callback_time_ms_{};
    std::atomic<uint32_t> redirects_count_{};
    std::chrono::milliseconds min_time_between_triggering_;
    uint32_t redirects_threshold_;
    RedirectCB cb_;
  };

  using ClusterInfoSharedPtr = std::shared_ptr<ClusterInfo>;

  class HandleImpl : public Handle {
  public:
    HandleImpl(RedirectionManagerImpl* mgr, ClusterInfoSharedPtr& cluster_info)
        : manager_(mgr->shared_from_this()), cluster_info_(cluster_info) {}

    ~HandleImpl() override { manager_->unregisterCluster(cluster_info_); }

  private:
    const std::shared_ptr<RedirectionManagerImpl> manager_;
    const std::shared_ptr<ClusterInfo> cluster_info_;
  };

  RedirectionManagerImpl(Event::Dispatcher& main_thread_dispatcher, Upstream::ClusterManager& cm,
                         TimeSource& time_source)
      : main_thread_dispatcher_(main_thread_dispatcher), cm_(cm), time_source_(time_source) {}

  bool onRedirection(const std::string& cluster_name) override;

  HandlePtr registerCluster(const std::string& cluster_name,
                            std::chrono::milliseconds min_time_between_triggering,
                            uint32_t redirects_threshold, const RedirectCB& cb) override;

private:
  void unregisterCluster(const ClusterInfoSharedPtr& cluster_info);

  Event::Dispatcher& main_thread_dispatcher_;
  Upstream::ClusterManager& cm_;
  TimeSource& time_source_;
  std::map<std::string, ClusterInfoSharedPtr> info_map_ GUARDED_BY(map_mutex_);
  Thread::MutexBasicLockable map_mutex_;
};

RedirectionManagerSharedPtr getRedirectionManager(Singleton::Manager& manager,
                                                  Event::Dispatcher& main_thread_dispatcher,
                                                  Upstream::ClusterManager& cm,
                                                  TimeSource& time_source);
} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
