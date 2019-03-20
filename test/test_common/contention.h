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
  void generateContention(MutexTracerImpl& tracer);

private:
  ThreadPtr launchThread(MutexTracerImpl& tracer);
  void holdUntilContention(MutexTracerImpl& tracer);

  MutexBasicLockable mutex_;
  DangerousDeprecatedTestTime test_time_;
};

} // namespace TestUtil
} // namespace Thread
} // namespace Envoy
