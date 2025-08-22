#include "source/common/common/notification.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Notification {

// This class implements the logic for triggering ENVOY_NOTIFICATION logs and actions.
class EnvoyNotificationRegistrationImpl : public Assert::ActionRegistration {
public:
  EnvoyNotificationRegistrationImpl(std::function<void(absl::string_view)> action)
      : action_(action) {
    next_action_ = envoy_notification_record_action_;
    envoy_notification_record_action_ = this;
  }

  ~EnvoyNotificationRegistrationImpl() override {
    ASSERT(envoy_notification_record_action_ == this);
    envoy_notification_record_action_ = next_action_;
  }

  void invoke(absl::string_view name) {
    action_(name);
    if (next_action_) {
      next_action_->invoke(name);
    }
  }

  static void invokeAction(absl::string_view name) {
    if (envoy_notification_record_action_ != nullptr) {
      envoy_notification_record_action_->invoke(name);
    }
  }

private:
  std::function<void(absl::string_view)> action_;
  EnvoyNotificationRegistrationImpl* next_action_ = nullptr;

  // Pointer to the first action in the chain or nullptr if no action is currently registered.
  static EnvoyNotificationRegistrationImpl* envoy_notification_record_action_;
};

EnvoyNotificationRegistrationImpl*
    EnvoyNotificationRegistrationImpl::envoy_notification_record_action_ = nullptr;

Assert::ActionRegistrationPtr
addEnvoyNotificationRecordAction(const std::function<void(absl::string_view)>& action) {
  return std::make_unique<EnvoyNotificationRegistrationImpl>(action);
}

namespace details {

void invokeEnvoyNotification(absl::string_view name) {
  EnvoyNotificationRegistrationImpl::invokeAction(name);
}

} // namespace details

} // namespace Notification
} // namespace Envoy
