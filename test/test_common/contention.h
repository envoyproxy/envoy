#pragma once

#include <string>
#include <vector>

#include "envoy/api/api.h"

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
  ContentionGenerator(Api::Api& api) : api_(api) {}

  /**
   * Generates at least once occurrence of mutex contention, as measured by tracer.
   */
  void generateContention(MutexTracerImpl& tracer);

private:
  ThreadPtr launchThread(MutexTracerImpl& tracer);
  void holdUntilContention(MutexTracerImpl& tracer);

  MutexBasicLockable mutex_;
  Api::Api& api_;
  std::atomic<bool> found_contention_{false};
};

} // namespace TestUtil
} // namespace Thread
} // namespace Envoy
