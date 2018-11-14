#pragma once

#include <string>
#include <vector>

#include "common/common/lock_guard.h"
#include "common/common/mutex_tracer_impl.h"
#include "common/common/thread.h"

#include "test/test_common/test_time.h"

namespace Envoy {
namespace Thread {
namespace TestUtil {

/**
 * Generates mutex contention, as measured by the MutexTracer passed in via argument.
 * @param tracer a reference to the global MutexTracerImpl.
 */

class ContentionGenerator {
public:
  /**
   * Generates at least once occurrence of mutex contention, as measured by tracer.
   */
  static void generateContention(MutexTracerImpl& tracer);

private:
  static Envoy::Thread::ThreadPtr launchThread(MutexTracerImpl& tracer, MutexBasicLockable* mu);

  static void holdUntilContention(MutexTracerImpl& tracer, MutexBasicLockable* mu);
};

} // namespace TestUtil
} // namespace Thread
} // namespace Envoy
