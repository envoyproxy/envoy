#pragma once

#include "common/common/assert.h"
#include "common/common/thread.h"

namespace Envoy {
namespace Thread {

class OnlyOneThread {
public:
  OnlyOneThread();

  /**
   * Ensures that one thread is used in a testcase to access some resource.
   */
  void checkOneThread();

 private:
  ThreadFactory& thread_factory_;
  ThreadIdPtr thread_advancing_time_ GUARDED_BY(mutex_);
  mutable MutexBasicLockable mutex_;
};

} // namespace Thread
} // namespace Envoy
