#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "envoy/thread/thread.h"

#include "source/common/common/logger.h"

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
 * The class is final, as the thread may still be running during the destructor
 * - this is fine so long as no class members or vtable entries have yet been
 * destroyed, which can be guaranteed if the class is final.
 *
 * See DESIGN.md for more details of the eviction process.
 **/
class CacheEvictionThread final : public Logger::Loggable<Logger::Id::cache_filter> {
public:
  CacheEvictionThread(Thread::ThreadFactory& thread_factory);

  /**
   * The destructor may block until the cache eviction thread is joined.
   */
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
   * Signals the cache eviction thread that it's time to check the current cache
   * state against any configured limits, and perform eviction if necessary.
   */
  void signal();

private:
  /**
   * The function that runs on the thread.
   *
   * This thread is expected to spend most of its time blocked, waiting for either
   * `signal` or `terminate` to be called, or a configured period.
   *
   * When unblocked, the thread will exit if terminating_ is set.
   *
   * Otherwise, each cache instance's `needsEviction` function is called, in an
   * arbitrary order, and, if that returns true, the `evict` function is also called.
   *
   * If `signal` is called during the eviction process, the eviction
   * cycle may run a second time after completion, depending on configured
   * constraints.
   */
  void work();

  /**
   * @return false if terminating, true if `signalled_` is true or the run-again period
   * has passed.
   */
  bool waitForSignal();

  /**
   * Notifies the thread to terminate. If it is currently evicting, it will
   * complete the current eviction cycle before exiting. If it is currently
   * idle, it will exit immediately (terminate does not wait for exiting
   * to be complete).
   */
  void terminate();

  // These two mutexes are never held at the same time. We signify this by requiring
  // that both be 'acquired before' the other, since there is no exclusion annotation.
  absl::Mutex mu_ ABSL_ACQUIRED_BEFORE(cache_mu_);
  bool signalled_ ABSL_GUARDED_BY(mu_) = false;
  bool terminating_ ABSL_GUARDED_BY(mu_) = false;

  absl::Mutex cache_mu_ ABSL_ACQUIRED_BEFORE(mu_);
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
