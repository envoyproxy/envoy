#include "source/common/common/assert.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Assert {

class ActionRegistrationImpl : public ActionRegistration {
public:
  ActionRegistrationImpl(std::function<void(const char* location)> action) : action_(action) {
    next_action_ = debug_assertion_failure_record_action_;
    debug_assertion_failure_record_action_ = this;
  }

  ~ActionRegistrationImpl() override {
    ASSERT(debug_assertion_failure_record_action_ == this);
    debug_assertion_failure_record_action_ = next_action_;
  }

  void invoke(const char* location) {
    action_(location);
    if (next_action_) {
      next_action_->invoke(location);
    }
  }

  static void invokeAction(const char* location) {
    if (debug_assertion_failure_record_action_ != nullptr) {
      debug_assertion_failure_record_action_->invoke(location);
    }
  }

private:
  std::function<void(const char* location)> action_;
  ActionRegistrationImpl* next_action_ = nullptr;

  // Pointer to the first action in the chain or nullptr if no action is currently registered.
  static ActionRegistrationImpl* debug_assertion_failure_record_action_;
};

class EnvoyBugState {
public:
  static EnvoyBugState& get() { MUTABLE_CONSTRUCT_ON_FIRST_USE(EnvoyBugState); }

  void clear() {
    absl::MutexLock lock(&mutex_);
    counters_.clear();
  }

  uint64_t inc(absl::string_view bug_name) {
    absl::MutexLock lock(&mutex_);
    return ++counters_[bug_name];
  }

private:
  absl::Mutex mutex_;
  absl::flat_hash_map<std::string, uint64_t> counters_ ABSL_GUARDED_BY(mutex_);
};

// This class implements the logic for triggering ENVOY_BUG logs and actions. Logging and actions
// will be triggered with exponential back-off per file and line bug.
class EnvoyBugRegistrationImpl : public ActionRegistration {
public:
  EnvoyBugRegistrationImpl(std::function<void(const char* location)> action) : action_(action) {
    next_action_ = envoy_bug_failure_record_action_;
    envoy_bug_failure_record_action_ = this;

    // Reset counters when a registration is added.
    EnvoyBugState::get().clear();
  }

  ~EnvoyBugRegistrationImpl() override {
    ASSERT(envoy_bug_failure_record_action_ == this);
    envoy_bug_failure_record_action_ = next_action_;
  }

  // This method is invoked when an ENVOY_BUG condition fails. It increments a per file and line
  // counter for every ENVOY_BUG hit in a mutex guarded map.
  // The implementation may also be a inline static counter per-file and line. There is no benchmark
  // to show that the performance of this mutex is any worse than atomic counters. Acquiring and
  // releasing a mutex is cheaper than a cache miss, but the mutex here is contended for every
  // ENVOY_BUG failure rather than per individual bug. Hitting ENVOY_BUGs is not a performance
  // critical path, and mutex contention would indicate that there is a serious failure.
  // Currently, this choice reduces code size and has the advantage that behavior is easier to
  // understand and debug, and test behavior is predictable.
  static bool shouldLogAndInvoke(absl::string_view bug_name) {
    // Increment counter, inserting first if counter does not exist.
    const uint64_t counter_value = EnvoyBugState::get().inc(bug_name);

    // Check if counter is power of two by its bitwise representation.
    return (counter_value & (counter_value - 1)) == 0;
  }

  void invoke(const char* location) {
    action_(location);
    if (next_action_) {
      next_action_->invoke(location);
    }
  }

  static void invokeAction(const char* location) {
    if (envoy_bug_failure_record_action_ != nullptr) {
      envoy_bug_failure_record_action_->invoke(location);
    }
  }

  static void resetEnvoyBugCounters() { EnvoyBugState::get().clear(); }

private:
  std::function<void(const char* location)> action_;
  EnvoyBugRegistrationImpl* next_action_ = nullptr;

  // Pointer to the first action in the chain or nullptr if no action is currently registered.
  static EnvoyBugRegistrationImpl* envoy_bug_failure_record_action_;
};

ActionRegistrationImpl* ActionRegistrationImpl::debug_assertion_failure_record_action_ = nullptr;
EnvoyBugRegistrationImpl* EnvoyBugRegistrationImpl::envoy_bug_failure_record_action_ = nullptr;

ActionRegistrationPtr
addDebugAssertionFailureRecordAction(const std::function<void(const char* location)>& action) {
  return std::make_unique<ActionRegistrationImpl>(action);
}

ActionRegistrationPtr
addEnvoyBugFailureRecordAction(const std::function<void(const char* location)>& action) {
  return std::make_unique<EnvoyBugRegistrationImpl>(action);
}

void invokeDebugAssertionFailureRecordActionForAssertMacroUseOnly(const char* location) {
  ActionRegistrationImpl::invokeAction(location);
}

void invokeEnvoyBugFailureRecordActionForEnvoyBugMacroUseOnly(const char* location) {
  EnvoyBugRegistrationImpl::invokeAction(location);
}

bool shouldLogAndInvokeEnvoyBugForEnvoyBugMacroUseOnly(absl::string_view bug_name) {
  return EnvoyBugRegistrationImpl::shouldLogAndInvoke(bug_name);
}

void resetEnvoyBugCountersForTest() { EnvoyBugRegistrationImpl::resetEnvoyBugCounters(); }

} // namespace Assert
} // namespace Envoy
