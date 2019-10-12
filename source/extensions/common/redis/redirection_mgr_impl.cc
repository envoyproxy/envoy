#include "extensions/common/redis/redirection_mgr_impl.h"

#include "envoy/singleton/manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

SINGLETON_MANAGER_REGISTRATION(redis_redirection_manager);

RedirectionManagerSharedPtr getRedirectionManager(Singleton::Manager& manager,
                                                  Event::Dispatcher& main_thread_dispatcher,
                                                  Upstream::ClusterManager& cm,
                                                  TimeSource& time_source) {
  return manager.getTyped<RedirectionManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(redis_redirection_manager), [&] {
        return std::make_shared<RedirectionManagerImpl>(main_thread_dispatcher, cm, time_source);
      });
}

bool RedirectionManagerImpl::onRedirection(const std::string& cluster_name) {
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
  if (info.get()) {
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       time_source_.monotonicTime().time_since_epoch())
                       .count();
    uint64_t last_callback_time_ms = info->last_callback_time_ms_.load();
    if (!last_callback_time_ms ||
        (now >= (last_callback_time_ms + info->min_time_between_triggering_.count()))) {
      info->redirects_count_++; // ignore redirects during min time between triggering
      if ((info->redirects_count_ >= info->redirects_threshold_) &&
          (info->last_callback_time_ms_.compare_exchange_strong(last_callback_time_ms, now))) {
        info->redirects_count_ = 0;
        main_thread_dispatcher_.post([=]() {
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

RedirectionManagerImpl::HandlePtr
RedirectionManagerImpl::registerCluster(const std::string& cluster_name,
                                        const std::chrono::milliseconds min_time_between_triggering,
                                        const uint32_t redirects_threshold, const RedirectCB cb) {

  ClusterInfoSharedPtr info = std::make_shared<ClusterInfo>(
      cluster_name, min_time_between_triggering, redirects_threshold, cb);
  info_map_[cluster_name] = info;

  std::unique_ptr<RedirectionManagerImpl::HandleImpl> handle =
      std::make_unique<RedirectionManagerImpl::HandleImpl>(this, info);
  return std::move(handle);
}

void RedirectionManagerImpl::unregisterCluster(ClusterInfoSharedPtr& cluster_info) {
  Thread::LockGuard lock(map_mutex_);
  info_map_.erase(cluster_info->cluster_name_);
}

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
