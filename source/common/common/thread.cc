#include "common/common/thread.h"

namespace Envoy {
namespace Thread {

bool MutexBasicLockable::awaitWithTimeout(const bool& condition, const Duration& timeout) {
  return mutex_.AwaitWithTimeout(absl::Condition(&condition), absl::FromChrono(timeout));
}

bool MutexBasicLockable::awaitWithTimeout(BoolFn check_condition, const Duration& timeout) {
  auto cond_no_capture = +[](BoolFn* check_condition) -> bool { return (*check_condition)(); };
  return mutex_.AwaitWithTimeout(absl::Condition(cond_no_capture, &check_condition),
                                 absl::FromChrono(timeout));
}

} // namespace Thread
} // namespace Envoy
