#pragma once

#include "common/common/logger.h"

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
  ~ThreadSynchronizer();

  /**
   * Enable the synchronizer. This should be called once by test code.
   */
  void enable();

  /**
   * Wait for a specified event to occur via signal(). Before blocking to wait, any callers of
   * barrier() waiting on this event will be released.
   */
  void wait(absl::string_view event_name) {
    if (data_ != nullptr) {
      waitWorker(event_name);
    }
  }

  /**
   * Tell the synchronizer to either ignore (or not) wait events. If ignore is true, any code
   * calling wait() will immediately proceed.
   */
  void ignoreWait(absl::string_view event_name, bool ignore) {
    if (data_ != nullptr) {
      ignoreWaitWorker(event_name, ignore);
    }
  }

  /**
   * Wait until another thread is itself waiting on an event. This can be used to force a
   * specific interleaving between two threads.
   */
  void barrier(absl::string_view event_name) {
    if (data_ != nullptr) {
      barrierWorker(event_name);
    }
  }

  /**
   * Signal an event such that a thread that has called wait() will now proceed.
   */
  void signal(absl::string_view event_name) {
    if (data_ != nullptr) {
      signalWorker(event_name);
    }
  }

private:
  struct SynchronizerEntry {
    absl::Mutex mutex_;
    bool signaled_ ABSL_GUARDED_BY(mutex_){};
    bool at_barrier_ ABSL_GUARDED_BY(mutex_){};
    bool ignore_wait_ ABSL_GUARDED_BY(mutex_){};
  };

  struct SynchronizerData {
    absl::Mutex mutex_;
    absl::flat_hash_map<std::string, std::unique_ptr<SynchronizerEntry>>
        entries_ ABSL_GUARDED_BY(mutex_);
  };

  SynchronizerEntry& getOrCreateEntry(absl::string_view event_name);
  void waitWorker(absl::string_view event_name);
  void ignoreWaitWorker(absl::string_view event_name, bool ignore);
  void barrierWorker(absl::string_view event_name);
  void signalWorker(absl::string_view event_name);

  std::unique_ptr<SynchronizerData> data_;
};

} // namespace Thread
} // namespace Envoy
