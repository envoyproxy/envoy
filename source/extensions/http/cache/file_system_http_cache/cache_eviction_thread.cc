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
  if (caches_.empty()) {
    terminate();
    thread_->join();
  }
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
  // Worth noting here that if `signalled_` is already true, the lock is not released
  // until idle_ is false again, so waitForIdle will not return until `signalled_`
  // stays false for the duration of an eviction cycle.
  idle_ = true;
  auto cond = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return signalled_; };
  mu_.Await(absl::Condition(&cond));
  signalled_ = false;
  idle_ = false;
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

void CacheEvictionThread::waitForIdle() {
  absl::MutexLock lock(&mu_);
  auto cond = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return idle_ && !signalled_; };
  mu_.Await(absl::Condition(&cond));
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
