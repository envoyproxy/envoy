#include "test/common/config/filesystem_subscription_test_harness.h"
#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class FilesystemSubscriptionImplTest : public FilesystemSubscriptionTestHarness,
                                       public testing::Test {};

// Validate that the client can recover from bad JSON responses.
TEST_F(FilesystemSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  updateFile(";!@#badjso n");
  verifyStats(2, 0, 0, 1, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  verifyStats(3, 1, 0, 1, 7148434200721666028);
}

// Validate that a file that is initially available results in a successful update.
TEST_F(FilesystemSubscriptionImplTest, InitialFile) {
  updateFile("{\"versionInfo\": \"0\", \"resources\": []}", false);
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 1, 0, 0, 7148434200721666028);
}

// Validate that if we fail to set a watch, we get a sensible warning.
TEST_F(FilesystemSubscriptionImplTest, BadWatch) {
  updateFile("", true);
  FilesystemEdsSubscriptionImpl subscription_(dispatcher_, "@/dev/null", stats_);
  EXPECT_LOG_CONTAINS(
      "warning",
      "Unable to set filesystem watch on @/dev/null. This path will not be watched for updates.",
      subscription_.start({}, callbacks_));
}

} // namespace
} // namespace Config
} // namespace Envoy
