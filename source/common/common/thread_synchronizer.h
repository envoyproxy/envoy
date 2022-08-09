#pragma once

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Thread {

/**
 * This class allows for forcing hard to test thread permutations. It is loosely modeled after:
 * https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/thread/thread_synchronizer.h
 *
 * The idea is that there is almost no cost if the synchronizer is not enabled by test code as
 * there is a single inline pointer check.
 */
class ThreadSynchronizer : Logger::Loggable<Logger::Id::misc> {
public:
  /**
   * Enable the synchronizer. This should be called once per test by test code.
   */
  void enable();

  /**
   * This is the only API that should generally be called from production code. It introduces
   * a "sync point" that test code can then use to force blocking, thread barriers, etc. Even
   * when the synchronizer is enabled(), the syncPoint() will do nothing unless it has been
   * registered to block via waitOn().
   */
  void syncPoint(absl::string_view event_name) {
    if (data_ != nullptr) {
      syncPointWorker(event_name);
    }
  }

  /**
   * The next time the sync point registered with event_name is invoked via syncPoint(), the calling
   * code will block until signaled. Note that this is a one-shot operation and the sync point's
   * wait status will be cleared.
   */
  void waitOn(absl::string_view event_name) {
    ASSERT(data_ != nullptr, "call enable() from test code before calling this method");
    waitOnWorker(event_name);
  }

  /**
   * This call will block until the next time the sync point registered with event_name is invoked.
   * The event_name must have been previously registered for blocking via waitOn(). The typical
   * test pattern is to have a thread arrive at a sync point, block, and then release a test
   * thread which continues test execution, eventually calling signal() to release the other thread.
   */
  void barrierOn(absl::string_view event_name) {
    ASSERT(data_ != nullptr, "call enable() from test code before calling this method");
    barrierOnWorker(event_name);
  }

  /**
   * Signal an event such that a thread that is blocked within syncPoint() will now proceed.
   */
  void signal(absl::string_view event_name) {
    ASSERT(data_ != nullptr, "call enable() from test code before calling this method");
    signalWorker(event_name);
  }

private:
  struct SynchronizerEntry {
    ~SynchronizerEntry() {
      // Make sure we don't have any pending signals which would indicate a bad test.
      ASSERT(!signaled_);
    }

    absl::Mutex mutex_;
    bool wait_on_ ABSL_GUARDED_BY(mutex_){};
    bool signaled_ ABSL_GUARDED_BY(mutex_){};
    bool at_barrier_ ABSL_GUARDED_BY(mutex_){};
  };

  struct SynchronizerData {
    absl::Mutex mutex_;
    absl::flat_hash_map<std::string, std::unique_ptr<SynchronizerEntry>>
        entries_ ABSL_GUARDED_BY(mutex_);
  };

  SynchronizerEntry& getOrCreateEntry(absl::string_view event_name);
  void syncPointWorker(absl::string_view event_name);
  void waitOnWorker(absl::string_view event_name);
  void barrierOnWorker(absl::string_view event_name);
  void signalWorker(absl::string_view event_name);

  std::unique_ptr<SynchronizerData> data_;
};

} // namespace Thread
} // namespace Envoy
