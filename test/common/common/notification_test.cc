#include "source/common/common/notification.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(EnvoyNotification, CallbackInvoked) {
  // Use 2 envoy notification action registrations to verify that action chaining is working
  // correctly.
  int envoy_notification_count = 0;
  int envoy_notification_count2 = 0;
  std::string name1;
  std::string name2;
  auto envoy_notification_action_registration =
      Notification::addEnvoyNotificationRecordAction([&](absl::string_view name) {
        name1 = name;
        envoy_notification_count++;
      });
  auto envoy_notification_action_registration2 =
      Notification::addEnvoyNotificationRecordAction([&](absl::string_view name) {
        name2 = name;
        envoy_notification_count2++;
      });

  EXPECT_LOG_CONTAINS("debug", "envoy notification: id1.", { ENVOY_NOTIFICATION("id1", ""); });
  EXPECT_EQ(envoy_notification_count, 1);
  EXPECT_EQ(envoy_notification_count2, 1);
  EXPECT_EQ(name1, "id1");
  EXPECT_EQ(name2, "id1");
  EXPECT_LOG_CONTAINS("debug", "envoy notification: .", { ENVOY_NOTIFICATION("", ""); });
  EXPECT_EQ(envoy_notification_count, 2);
  EXPECT_EQ(envoy_notification_count2, 2);
  EXPECT_EQ(name1, "");
  EXPECT_EQ(name2, "");
  EXPECT_LOG_CONTAINS("debug", "envoy notification: id2. Details: with some logs",
                      { ENVOY_NOTIFICATION("id2", "with some logs"); });
  EXPECT_EQ(envoy_notification_count, 3);
  EXPECT_EQ(envoy_notification_count2, 3);
  EXPECT_EQ(name1, "id2");
  EXPECT_EQ(name2, "id2");
}

} // namespace Envoy
