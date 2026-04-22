#pragma once

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Notification {

/**
 * Sets an action to be invoked when an ENVOY_NOTIFICATION is encountered.
 *
 * This function is not thread-safe; concurrent calls to set the action are not allowed.
 *
 * The action may be invoked concurrently if two ENVOY_NOTIFICATION in different threads run at the
 * same time, so the action must be thread-safe.
 *
 * The action will be invoked in all build types (debug or release).
 *
 * @param action The action to take when an envoy bug fails.
 * @return A registration object. The registration is removed when the object is destructed.
 */
Assert::ActionRegistrationPtr
addEnvoyNotificationRecordAction(const std::function<void(absl::string_view)>& action);

namespace details {
/**
 * Invokes the action set by addEnvoyNotificationRecordAction, or does nothing if
 * no action has been set.
 *
 * @param location Unique identifier for the ENVOY_NOTIFICATION.
 *
 * This should only be called by ENVOY_NOTIFICATION macros in this file.
 */
void invokeEnvoyNotification(absl::string_view name);
} // namespace details

#define _ENVOY_NOTIFICATION_IMPL(NAME, DETAILS)                                                    \
  do {                                                                                             \
    const std::string& details = (DETAILS);                                                        \
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::notification), debug,   \
                        "envoy notification: {}.{}{}", NAME,                                       \
                        details.empty() ? "" : " Details: ", details);                             \
    Envoy::Notification::details::invokeEnvoyNotification((NAME));                                 \
  } while (false)

/**
 * Invoke a notification of a specific condition. In contrast to ENVOY_BUG it does not ASSERT in
 * debug builds and as such has no impact on continuous integration or system tests. If a condition
 * is met it is logged at the debug verbosity and a stat is incremented. There is no exponential
 * backoff, so the notification will be invoked every time the condition is met. As such
 * notification handler must have low overhead if the condition is expected to be encountered
 * frequently. ENVOY_NOTIFICATION must be called with three arguments for verbose logging.
 */
#define ENVOY_NOTIFICATION(...) PASS_ON(PASS_ON(_ENVOY_NOTIFICATION_IMPL)(__VA_ARGS__))

} // namespace Notification
} // namespace Envoy
