#include "source/extensions/http/cache/file_system_http_cache/cache_eviction_thread.h"

#include "envoy/thread/thread.h"

#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

CacheEvictionThread::CacheEvictionThread(Thread::ThreadFactory& thread_factory)
    : thread_(thread_factory.createThread([this]() { work(); })) {}

CacheEvictionThread::~CacheEvictionThread() {
  terminate();
  thread_->join();
}

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
  // Worth noting here that if `signalled_` is already true, the lock is not released
  // until idle_ is false again, so waitForIdle will not return until `signalled_`
  // stays false for the duration of an eviction cycle.
  idle_ = true;
  mu_.Await(absl::Condition(&signalled_));
  signalled_ = false;
  idle_ = false;
  return !terminating_;
}

void CacheEvictionThread::work() {
  while (waitForSignal()) {
    absl::MutexLock lock(&cache_mu_);
    // This lock must be held for the duration of the evictions, as the cache pointers
    // are not owned by CacheEvictionThread, and can be deleted any time the lock is
    // not held.
    //
    // We can't use weak_ptr and transition to shared_ptr, because the caches own the
    // CacheEvictionThread - if the CacheEvictionThread temporarily owns a cache, it
    // can end up indirectly being the sole owner of itself, such that when those
    // pointers go out of scope it can end up calling its own destructor - if that
    // happens then it tries to pthread_join from the thread itself, which is an error.
    //
    // Therefore, we must have this distasteful long-lived hold on the mutex.
    //
    // The only other users of cache_mu_, however, are addCache and removeCache.
    // Therefore this should only block filter configuration updates that create or
    // destroy caches; as such the long-held lock should not be problematic in practice.
    for (FileSystemHttpCache* cache : caches_) {
      cache->maybeEvict();
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
