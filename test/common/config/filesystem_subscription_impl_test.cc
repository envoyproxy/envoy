#include "test/common/config/filesystem_subscription_test_harness.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Throw;

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
TEST(MiscFilesystemSubscriptionImplTest, BadWatch) {
  Event::MockDispatcher dispatcher;
  Stats::MockIsolatedStatsStore stats_store;
  SubscriptionStats stats{Utility::generateStats(stats_store)};
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _)).WillOnce(Throw(EnvoyException("bad path")));
  EXPECT_THROW_WITH_MESSAGE(FilesystemEdsSubscriptionImpl(dispatcher, "##!@/dev/null", stats),
                            EnvoyException, "bad path");
}

} // namespace
} // namespace Config
} // namespace Envoy
