#include "common/common/assert.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Assert {

class ActionRegistrationImpl : public ActionRegistration {
public:
  ActionRegistrationImpl(std::function<void()> action) {
    ASSERT(debug_assertion_failure_record_action_ == nullptr);
    debug_assertion_failure_record_action_ = action;
  }

  ~ActionRegistrationImpl() override {
    ASSERT(debug_assertion_failure_record_action_ != nullptr);
    debug_assertion_failure_record_action_ = nullptr;
  }

  static void invokeAction() {
    if (debug_assertion_failure_record_action_ != nullptr) {
      debug_assertion_failure_record_action_();
    }
  }

private:
  // This implementation currently only handles one action being set at a time. This is currently
  // sufficient. If multiple actions are ever needed, the actions should be chained when
  // additional actions are registered.
  static std::function<void()> debug_assertion_failure_record_action_;
};

// This class implements the logic for triggering ENVOY_BUG logs and actions. Logging and actions
// will be triggered with exponential back-off per file and line bug.
class EnvoyBugRegistrationImpl : public ActionRegistration {
public:
  EnvoyBugRegistrationImpl(std::function<void()> action) {
    ASSERT(envoy_bug_failure_record_action_ == nullptr,
           "An ENVOY_BUG action was already set. Currently only a single action is supported.");
    envoy_bug_failure_record_action_ = action;
    counters_.clear();
  }

  ~EnvoyBugRegistrationImpl() override {
    ASSERT(envoy_bug_failure_record_action_ != nullptr);
    envoy_bug_failure_record_action_ = nullptr;
  }

  // This method is invoked when an ENVOY_BUG condition fails. It increments a per file and line
  // counter for every ENVOY_BUG hit in a mutex guarded map.
  // The implementation may also be a inline static counter per-file and line. There is no benchmark
  // to show that the performance of this mutex is any worse than atomic counters. Acquiring and
  // releasing a mutex is cheaper than a cache miss, but the mutex here is contended for every
  // ENVOY_BUG failure rather than per individual bug. Logging ENVOY_BUGs is not a performance
  // critical path, and mutex contention would indicate that there is a serious failure.
  // Currently, this choice reduces code size and has the advantage that behavior is easier to
  // understand and debug, and test behavior is predictable.
  static bool shouldLogAndInvoke(absl::string_view bug_name) {
    // Increment counter, inserting first if counter does not exist.
    uint64_t counter_value = 0;
    {
      absl::MutexLock lock(&mutex_);
      counter_value = ++counters_[bug_name];
    }

    // Check if counter is power of two by its bitwise representation.
    return (counter_value & (counter_value - 1)) == 0;
  }

  static void invokeAction() {
    if (envoy_bug_failure_record_action_ != nullptr) {
      envoy_bug_failure_record_action_();
    }
  }

private:
  // This implementation currently only handles one action being set at a time. This is currently
  // sufficient. If multiple actions are ever needed, the actions should be chained when
  // additional actions are registered.
  static std::function<void()> envoy_bug_failure_record_action_;

  using EnvoyBugMap = absl::flat_hash_map<std::string, uint64_t>;
  static absl::Mutex mutex_;
  static EnvoyBugMap counters_ GUARDED_BY(mutex_);
};

std::function<void()> ActionRegistrationImpl::debug_assertion_failure_record_action_;
std::function<void()> EnvoyBugRegistrationImpl::envoy_bug_failure_record_action_;
EnvoyBugRegistrationImpl::EnvoyBugMap EnvoyBugRegistrationImpl::counters_;
absl::Mutex EnvoyBugRegistrationImpl::mutex_;

ActionRegistrationPtr setDebugAssertionFailureRecordAction(const std::function<void()>& action) {
  return std::make_unique<ActionRegistrationImpl>(action);
}

ActionRegistrationPtr setEnvoyBugFailureRecordAction(const std::function<void()>& action) {
  return std::make_unique<EnvoyBugRegistrationImpl>(action);
}

void invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly() {
  ActionRegistrationImpl::invokeAction();
}

void invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly() {
  EnvoyBugRegistrationImpl::invokeAction();
}

bool shouldLogAndInvokeEnvoyBugForEnvoyBugMacroUseOnly(absl::string_view bug_name) {
  return EnvoyBugRegistrationImpl::shouldLogAndInvoke(bug_name);
}

} // namespace Assert
} // namespace Envoy
