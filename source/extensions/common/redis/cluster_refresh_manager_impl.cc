#include "extensions/common/redis/cluster_refresh_manager_impl.h"

#include "envoy/singleton/manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

SINGLETON_MANAGER_REGISTRATION(redis_refresh_manager);

ClusterRefreshManagerSharedPtr getClusterRefreshManager(Singleton::Manager& manager,
                                                        Event::Dispatcher& main_thread_dispatcher,
                                                        Upstream::ClusterManager& cm,
                                                        TimeSource& time_source) {
  return manager.getTyped<ClusterRefreshManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(redis_refresh_manager), [&] {
        return std::make_shared<ClusterRefreshManagerImpl>(main_thread_dispatcher, cm, time_source);
      });
}

bool ClusterRefreshManagerImpl::onFailure(const std::string& cluster_name) {
  return onEvent(cluster_name, EventType::Failure);
}

bool ClusterRefreshManagerImpl::onHostDegraded(const std::string& cluster_name) {
  return onEvent(cluster_name, EventType::DegradedHost);
}

bool ClusterRefreshManagerImpl::onRedirection(const std::string& cluster_name) {
  return onEvent(cluster_name, EventType::Redirection);
}

bool ClusterRefreshManagerImpl::onEvent(const std::string& cluster_name, EventType event_type) {
  ClusterInfoSharedPtr info;
  {
    // Hold the map lock to avoid a race condition with calls to unregisterCluster
    // on the main thread.
    Thread::LockGuard lock(map_mutex_);
    auto it = info_map_.find(cluster_name);
    if (it != info_map_.end()) {
      info = it->second;
    }
  }
  // No locks needed for thread safety while accessing clusterInfoSharedPtr members as
  // all potentially modified members are atomic (redirects_count_, last_callback_time_ms_).
  if (info.get()) {
    const uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             time_source_.monotonicTime().time_since_epoch())
                             .count();
    uint64_t last_callback_time_ms = info->last_callback_time_ms_.load();
    if (!last_callback_time_ms ||
        (now >= (last_callback_time_ms + info->min_time_between_triggering_.count()))) {
      std::atomic<uint32_t>* count;
      uint32_t threshold;
      switch (event_type) {
      case EventType::Redirection: {
        count = &(info->redirects_count_);
        threshold = info->redirects_threshold_;
        break;
      }
      case EventType::DegradedHost: {
        count = &(info->host_degraded_count_);
        threshold = info->host_degraded_threshold_;
        break;
      }
      case EventType::Failure: {
        count = &(info->failures_count_);
        threshold = info->failure_threshold_;
        break;
      }
      }
      if (threshold <= 0) {
        return false;
      }
      // ignore redirects during min time between triggering
      if ((++(*count) >= threshold) &&
          (info->last_callback_time_ms_.compare_exchange_strong(last_callback_time_ms, now))) {
        // last_callback_time_ms_ successfully updated without any changes since it was
        // initially read. This thread is allowed to post a call to the registered callback
        // on the main thread. Otherwise, the thread would be ignored to prevent over-triggering
        // cluster callbacks.
        *count = 0;
        main_thread_dispatcher_.post([this, cluster_name, info]() {
          // Ensure that cluster is still active before calling callback.
          auto map = cm_.clusters();
          auto it = map.find(cluster_name);
          if (it != map.end()) {
            info->cb_();
          }
        });
        return true;
      }
    }
  }
  return false;
}

ClusterRefreshManagerImpl::HandlePtr ClusterRefreshManagerImpl::registerCluster(
    const std::string& cluster_name, std::chrono::milliseconds min_time_between_triggering,
    const uint32_t redirects_threshold, const uint32_t failure_threshold,
    const uint32_t host_degraded_threshold, const RefreshCB& cb) {
  Thread::LockGuard lock(map_mutex_);
  ClusterInfoSharedPtr info =
      std::make_shared<ClusterInfo>(cluster_name, min_time_between_triggering, redirects_threshold,
                                    failure_threshold, host_degraded_threshold, cb);
  info_map_[cluster_name] = info;

  return std::make_unique<ClusterRefreshManagerImpl::HandleImpl>(this, info);
}

void ClusterRefreshManagerImpl::unregisterCluster(const ClusterInfoSharedPtr& cluster_info) {
  Thread::LockGuard lock(map_mutex_);
  info_map_.erase(cluster_info->cluster_name_);
}

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
