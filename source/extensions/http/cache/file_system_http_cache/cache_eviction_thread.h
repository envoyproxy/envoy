#pragma once

#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "source/common/api/os_sys_calls_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

class FileSystemHttpCache;

/**
 * A class which controls a thread on which cache evictions for all instances
 * of FileSystemHttpCache are performed.
 *
 * The instance of CacheEvictionThread is owned by the `CacheSingleton`, which is
 * destroyed only when all cache instances have been destroyed. During destruction, the
 * thread is terminated and joined.
 **/
class CacheEvictionThread {
public:
  CacheEvictionThread();
  ~CacheEvictionThread();

  /**
   * Adds the given cache to the caches that may be evicted from.
   * @param cache a weak pointer to the cache in question.
   */
  void addCache(std::weak_ptr<FileSystemHttpCache> cache);

  /**
   * Signals the cache eviction thread that it's time to test things.
   * After receiving a signal, the thread will exit if terminating_ is set.
   * Otherwise it will call each cache's `maybeEvict` function in an arbitrary
   * order.
   */
  void signal();

private:
  /**
   * The function that runs on the thread.
   */
  void work();

  /**
   * Get the set of caches the thread should potentially evict from.
   * This also erases from caches_ any weak_ptrs whose target has been deleted.
   * @return a vector of shared_ptrs for each of the cache instances that still exist.
   */
  std::vector<std::shared_ptr<FileSystemHttpCache>> cachesToCheck();

  absl::Mutex mu_;
  bool signalled_ ABSL_GUARDED_BY(mu_) = false;
  bool terminating_ ABSL_GUARDED_BY(mu_) = false;
  absl::Mutex cache_mu_;
  std::vector<std::weak_ptr<FileSystemHttpCache>> caches_ ABSL_GUARDED_BY(cache_mu_);
  Api::OsSysCalls& os_sys_calls_;
  std::thread thread_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
