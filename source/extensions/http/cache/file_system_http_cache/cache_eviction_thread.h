#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "envoy/thread/thread.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

struct CacheShared;

/**
 * A class which controls a thread on which cache evictions for all instances
 * of FileSystemHttpCache are performed.
 *
 * The instance of CacheEvictionThread is owned by the `CacheSingleton`, which is
 * created only when a first cache instance is created, and destroyed only when
 * all cache instances have been destroyed.
 *
 * See DESIGN.md for more details of the eviction process.
 **/
class CacheEvictionThread {
public:
  CacheEvictionThread(Thread::ThreadFactory& thread_factory);
  ~CacheEvictionThread();

  /**
   * Adds the given cache to the caches that may be evicted from.
   * @param cache an unowned reference to the cache in question.
   */
  void addCache(std::shared_ptr<CacheShared> cache);

  /**
   * Removes the given cache from the caches that may be evicted from.
   * @param cache an unowned reference to the cache in question.
   */
  void removeCache(std::shared_ptr<CacheShared>& cache);

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
   * Initialize the cache stats.
   * @param cache the shared data of the cache to initialize.
   */

  static void init(CacheShared& cache);

  /**
   * Evict from the cache, until the configured thresholds are not exceeded.
   * @param cache the shared data of the cache to evict from.
   */
  static void evict(CacheShared& cache);

  /**
   * @return false if terminating.
   */
  bool waitForSignal();
  void terminate();

  absl::Mutex mu_;
  bool signalled_ ABSL_GUARDED_BY(mu_) = false;
  bool terminating_ ABSL_GUARDED_BY(mu_) = false;

  absl::Mutex cache_mu_;
  // We must store the caches as unowned references so they can be destroyed
  // during config changes - that destruction is the only signal that a cache
  // instance should be removed.
  absl::flat_hash_set<std::shared_ptr<CacheShared>> caches_ ABSL_GUARDED_BY(cache_mu_);

  // Allow test access to waitForIdle for synchronization.
  friend class FileSystemCacheTestContext;
  bool idle_ ABSL_GUARDED_BY(mu_) = false;
  void waitForIdle();

  // It is important that thread_ be last, as the new thread runs with 'this' and
  // may access any other members. If thread_ is not last, there can be a race between
  // that thread and the initialization of other members.
  Thread::ThreadPtr thread_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
