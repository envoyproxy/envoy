#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "test/common/config/filesystem_subscription_test_harness.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Config {
namespace {

class FilesystemSubscriptionImplTest : public testing::Test,
                                       public FilesystemSubscriptionTestHarness {};

// Validate that the client can recover from bad JSON responses.
TEST_F(FilesystemSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _));
  updateFile(";!@#badjso n");
  EXPECT_TRUE(statsAre(2, 0, 0, 1, 0, 0, 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 1, 0, 1, 0, TEST_TIME_MILLIS, 7148434200721666028));
}

// Validate that a file that is initially available results in a successful update.
TEST_F(FilesystemSubscriptionImplTest, InitialFile) {
  updateFile("{\"versionInfo\": \"0\", \"resources\": []}", false);
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 1, 0, 0, 0, TEST_TIME_MILLIS, 7148434200721666028));
}

// Validate that if we fail to set a watch, we get a sensible warning.
TEST(MiscFilesystemSubscriptionImplTest, BadWatch) {
  Event::MockDispatcher dispatcher;
  Stats::MockIsolatedStatsStore stats_store;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  SubscriptionStats stats{Utility::generateStats(stats_store)};
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _)).WillOnce(Throw(EnvoyException("bad path")));
  NiceMock<Config::MockSubscriptionCallbacks> callbacks;
  EXPECT_THROW_WITH_MESSAGE(FilesystemSubscriptionImpl(dispatcher, "##!@/dev/null", callbacks,
                                                       stats, validation_visitor, *api),
                            EnvoyException, "bad path");
}

// Validate that the update_time statistic isn't changed when the configuration update gets
// rejected.
TEST_F(FilesystemSubscriptionImplTest, UpdateTimeNotChangedOnUpdateReject) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _));
  updateFile(";!@#badjso n");
  EXPECT_TRUE(statsAre(2, 0, 0, 1, 0, 0, 0));
}

// Validate that the update_time statistic is changed after a trivial configuration update
// (update that resulted in no change).
TEST_F(FilesystemSubscriptionImplTest, UpdateTimeChangedOnUpdateSuccess) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 7148434200721666028));
  // Advance the simulated time.
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 1)));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 2, 0, 0, 0, TEST_TIME_MILLIS + 1, 7148434200721666028));
}

} // namespace
} // namespace Config
} // namespace Envoy
