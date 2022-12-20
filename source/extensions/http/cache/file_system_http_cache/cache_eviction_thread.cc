#include "source/extensions/http/cache/file_system_http_cache/cache_eviction_thread.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

CacheEvictionThread::CacheEvictionThread(Thread::ThreadFactory& thread_factory)
    : os_sys_calls_(Api::OsSysCallsSingleton::get()),
      thread_(thread_factory.createThread([this]() { work(); })) {}

void CacheEvictionThread::addCache(FileSystemHttpCache& cache) {
  absl::MutexLock lock(&cache_mu_);
  bool inserted = caches_.emplace(&cache).second;
  ASSERT(inserted);
}

void CacheEvictionThread::removeCache(FileSystemHttpCache& cache) {
  absl::MutexLock lock(&cache_mu_);
  bool removed = caches_.erase(&cache);
  ASSERT(removed);
}

void CacheEvictionThread::signal() {
  absl::MutexLock lock(&mu_);
  signalled_ = true;
}

void CacheEvictionThread::terminate() {
  absl::MutexLock lock(&mu_);
  terminating_ = true;
  signalled_ = true;
}

bool CacheEvictionThread::waitForSignal() {
  absl::MutexLock lock(&mu_);
  auto cond = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return signalled_; };
  mu_.Await(absl::Condition(&cond));
  signalled_ = false;
  return !terminating_;
}

void CacheEvictionThread::work() {
  while (waitForSignal()) {
    absl::MutexLock lock(&cache_mu_);
    for (FileSystemHttpCache* cache : caches_) {
      cache->maybeEvict(os_sys_calls_);
    }
  }
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
