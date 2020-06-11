#include "common/common/assert.h"

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/strings/str_join.h"

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

  static bool shouldLogAndInvoke(const char* filename, int line) {
    const auto name = absl::StrCat(filename, ",", line);

    // Increment counter, inserting first if counter does not exist.
    absl::ReleasableMutexLock lock(&mutex_);
    auto counter_value = ++counters_[name];
    lock.Release();

    // Check if counter is power of two by its bitwise representation.
    if ((counter_value & (counter_value - 1)) == 0) {
      return true;
    }
    return false;
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

bool shouldLogAndInvokeEnvoyBugForEnvoyBugMacroUseOnly(const char* filename, int line) {
  return EnvoyBugRegistrationImpl::shouldLogAndInvoke(filename, line);
}

} // namespace Assert
} // namespace Envoy
