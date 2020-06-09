#include "common/common/assert.h"

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
    count_ = 0;
    envoy_bug_failure_record_action_ = action;
  }

  ~EnvoyBugRegistrationImpl() override {
    ASSERT(envoy_bug_failure_record_action_ != nullptr);
    envoy_bug_failure_record_action_ = nullptr;
  }

  static bool shouldLogAndInvoke() {
    ++count_;
    // Check if count_ is power of two by its bitwise representation.
    if ((count_ & (count_ - 1)) == 0) {
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
  static std::atomic<uint64_t> count_;
};

std::function<void()> ActionRegistrationImpl::debug_assertion_failure_record_action_;
std::function<void()> EnvoyBugRegistrationImpl::envoy_bug_failure_record_action_;
std::atomic<uint64_t> EnvoyBugRegistrationImpl::count_;

ActionRegistrationPtr setDebugAssertionFailureRecordAction(const std::function<void()>& action) {
  return std::make_unique<ActionRegistrationImpl>(action);
}

ActionRegistrationPtr setEnvoyBugFailureRecordAction(const std::function<void()>& action) {
  return std::make_unique<EnvoyBugRegistrationImpl>(action);
}

void invokeDebugAssertionFailureRecordAction_ForAssertMacroUseOnly() {
  ActionRegistrationImpl::invokeAction();
}

void invokeEnvoyBugFailureRecordAction_ForEnvoyBugMacroUseOnly() {
  EnvoyBugRegistrationImpl::invokeAction();
}

bool shouldLogAndInvokeEnvoyBug_ForEnvoyBugMacroUseOnly() {
  return EnvoyBugRegistrationImpl::shouldLogAndInvoke();
}

} // namespace Assert
} // namespace Envoy
