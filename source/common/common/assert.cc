#include "common/common/assert.h"

namespace Envoy {
namespace Assert {

class ActionRegistrationImpl : public ActionRegistration {
public:
  ActionRegistrationImpl(std::function<void()> action) {
    ASSERT(debugAssertionFailureRecordAction_ == nullptr);
    debugAssertionFailureRecordAction_ = action;
  }

  ~ActionRegistrationImpl() {
    ASSERT(debugAssertionFailureRecordAction_ != nullptr);
    debugAssertionFailureRecordAction_ = nullptr;
  }

  static void invokeAction() {
    if (debugAssertionFailureRecordAction_ != nullptr) {
      debugAssertionFailureRecordAction_();
    }
  }

private:
  // This implementation currently only handles one action being set at a time. This is currently
  // sufficient. If multiple actions are ever needed, the actions should be chained when
  // additional actions are registered.
  static std::function<void()> debugAssertionFailureRecordAction_;
};

std::function<void()> ActionRegistrationImpl::debugAssertionFailureRecordAction_;

ActionRegistrationPtr setDebugAssertionFailureRecordAction(std::function<void()> action) {
  return std::make_unique<ActionRegistrationImpl>(action);
}

void invokeDebugAssertionFailureRecordAction_ForAssertMacroUseOnly() {
  ActionRegistrationImpl::invokeAction();
}

} // namespace Assert
} // namespace Envoy
