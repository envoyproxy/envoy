#include "test/common/config/filesystem_subscription_test_harness.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class FilesystemSubscriptionImplTest : public FilesystemSubscriptionTestHarness,
                                       public testing::Test {};

// Validate that the client can recover from bad JSON responses.
TEST_F(FilesystemSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  updateFile(";!@#badjso n");
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

} // namespace
} // namespace Config
} // namespace Envoy
