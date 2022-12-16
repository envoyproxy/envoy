#include "source/extensions/http/cache/file_system_http_cache/cache_eviction_thread.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

CacheEvictionThread::CacheEvictionThread()
    : os_sys_calls_(Api::OsSysCallsSingleton::get()), thread_([this]() { work(); }) {}

CacheEvictionThread::~CacheEvictionThread() {
  {
    absl::MutexLock lock(&mu_);
    terminating_ = true;
    signalled_ = true;
  }
  thread_.join();
}

void CacheEvictionThread::addCache(std::weak_ptr<FileSystemHttpCache> cache) {
  absl::MutexLock lock(&cache_mu_);
  caches_.push_back(std::move(cache));
}

void CacheEvictionThread::signal() {
  absl::MutexLock lock(&mu_);
  signalled_ = true;
}

std::vector<std::shared_ptr<FileSystemHttpCache>> CacheEvictionThread::cachesToCheck() {
  std::vector<std::shared_ptr<FileSystemHttpCache>> caches_to_check;
  absl::MutexLock lock(&cache_mu_);
  auto it = std::partition(caches_.begin(), caches_.end(),
                           [&caches_to_check](std::weak_ptr<FileSystemHttpCache> cache) {
                             std::shared_ptr<FileSystemHttpCache> locked_cache = cache.lock();
                             if (!locked_cache) {
                               return false;
                             }
                             caches_to_check.push_back(std::move(locked_cache));
                             return true;
                           });
  caches_.erase(it, caches_.end());
  return caches_to_check;
}

void CacheEvictionThread::work() {
  while (true) {
    {
      absl::MutexLock lock(&mu_);
      auto cond = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return signalled_; };
      mu_.Await(absl::Condition(&cond));
      signalled_ = false;
      if (terminating_) {
        return;
      }
    }
    std::vector<std::shared_ptr<FileSystemHttpCache>> caches_to_check = cachesToCheck();
    while (!caches_to_check.empty()) {
      std::shared_ptr<FileSystemHttpCache> cache = std::move(caches_to_check.back());
      caches_to_check.pop_back();
      cache->maybeEvict(os_sys_calls_);
    }
  }
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
