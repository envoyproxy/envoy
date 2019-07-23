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

std::function<void()> ActionRegistrationImpl::debug_assertion_failure_record_action_;

ActionRegistrationPtr setDebugAssertionFailureRecordAction(const std::function<void()>& action) {
  return std::make_unique<ActionRegistrationImpl>(action);
}

void invokeDebugAssertionFailureRecordAction_ForAssertMacroUseOnly() {
  ActionRegistrationImpl::invokeAction();
}

} // namespace Assert
} // namespace Envoy
