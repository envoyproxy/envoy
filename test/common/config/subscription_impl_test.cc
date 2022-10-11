#include <chrono>
#include <memory>

#include "test/common/config/delta_subscription_test_harness.h"
#include "test/common/config/filesystem_subscription_test_harness.h"
#include "test/common/config/grpc_subscription_test_harness.h"
#include "test/common/config/http_subscription_test_harness.h"
#include "test/common/config/subscription_test_harness.h"
#include "test/test_common/simulated_time_system.h"

using testing::InSequence;

namespace Envoy {
namespace Config {
namespace {

enum class SubscriptionType {
  Grpc,
  DeltaGrpc,
  UnifiedGrpc,
  UnifiedDeltaGrpc,
  Http,
  Filesystem,
};

// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const SubscriptionType sub, std::ostream* os) {
  (*os) << ([sub]() -> absl::string_view {
    switch (sub) {
    case SubscriptionType::Grpc:
      return "Grpc";
    case SubscriptionType::DeltaGrpc:
      return "DeltaGrpc";
    case SubscriptionType::Http:
      return "Http";
    case SubscriptionType::Filesystem:
      return "Filesystem";
    default:
      return "unknown";
    }
  })();
}

class SubscriptionImplTest : public testing::TestWithParam<SubscriptionType>,
                             public Event::TestUsingSimulatedTime {
public:
  SubscriptionImplTest() : SubscriptionImplTest(std::chrono::milliseconds(0)) {}
  SubscriptionImplTest(std::chrono::milliseconds init_fetch_timeout) {
    initialize(init_fetch_timeout);
  }

  void initialize(std::chrono::milliseconds init_fetch_timeout = std::chrono::milliseconds(0)) {
    switch (GetParam()) {
    case SubscriptionType::Grpc:
      test_harness_ = std::make_unique<GrpcSubscriptionTestHarness>(LegacyOrUnified::Legacy,
                                                                    init_fetch_timeout);
      break;
    case SubscriptionType::DeltaGrpc:
      test_harness_ = std::make_unique<GrpcSubscriptionTestHarness>(LegacyOrUnified::Legacy,
                                                                    init_fetch_timeout);
      break;
    case SubscriptionType::UnifiedGrpc:
      test_harness_ = std::make_unique<GrpcSubscriptionTestHarness>(LegacyOrUnified::Unified,
                                                                    init_fetch_timeout);
      break;
    case SubscriptionType::UnifiedDeltaGrpc:
      test_harness_ = std::make_unique<GrpcSubscriptionTestHarness>(LegacyOrUnified::Unified,
                                                                    init_fetch_timeout);
      break;
    case SubscriptionType::Http:
      test_harness_ = std::make_unique<HttpSubscriptionTestHarness>(init_fetch_timeout);
      break;
    case SubscriptionType::Filesystem:
      test_harness_ = std::make_unique<FilesystemSubscriptionTestHarness>();
      break;
    }
  }

  void TearDown() override { test_harness_->doSubscriptionTearDown(); }

  virtual void startSubscription(const std::set<std::string>& cluster_names) {
    expectCreateEnablePeriodicStatsTimer(
        std::chrono::milliseconds(PERIODIC_STATS_TIMER_REFRESH_MS));
    test_harness_->startSubscription(cluster_names);
  }

  void updateResourceInterest(const std::set<std::string>& cluster_names) {
    test_harness_->updateResourceInterest(cluster_names);
  }

  void expectSendMessage(const std::set<std::string>& cluster_names, const std::string& version,
                         bool expect_node) {
    test_harness_->expectSendMessage(cluster_names, version, expect_node);
  }

  AssertionResult statsAre(uint32_t attempt, uint32_t success, uint32_t rejected, uint32_t failure,
                           uint32_t init_fetch_timeout, uint64_t update_time, uint64_t version,
                           std::string version_text, uint64_t time_since_last_update) {
    return test_harness_->statsAre(attempt, success, rejected, failure, init_fetch_timeout,
                                   update_time, version, version_text, time_since_last_update);
  }

  void deliverConfigUpdate(const std::vector<std::string> cluster_names, const std::string& version,
                           bool accept) {
    test_harness_->deliverConfigUpdate(cluster_names, version, accept);
  }

  void expectConfigUpdateFailed() { test_harness_->expectConfigUpdateFailed(); }

  void expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds timeout) {
    test_harness_->expectEnableInitFetchTimeoutTimer(timeout);
  }

  void expectDisableInitFetchTimeoutTimer() { test_harness_->expectDisableInitFetchTimeoutTimer(); }

  void callInitFetchTimeoutCb() { test_harness_->callInitFetchTimeoutCb(); }

  void expectCreateEnablePeriodicStatsTimer(std::chrono::milliseconds period) {
    test_harness_->expectCreateEnablePeriodicStatsTimer(period);
  }

  void expectEnablePeriodicStatsTimer(std::chrono::milliseconds period) {
    test_harness_->expectEnablePeriodicStatsTimer(period);
  }

  void expectDisablePeriodicStatsTimer() { test_harness_->expectDisablePeriodicStatsTimer(); }

  void callPeriodicStatsTimerCb() { test_harness_->callPeriodicStatsTimerCb(); }

  std::unique_ptr<SubscriptionTestHarness> test_harness_;
};

class SubscriptionImplInitFetchTimeoutTest : public SubscriptionImplTest {
public:
  SubscriptionImplInitFetchTimeoutTest() : SubscriptionImplTest(std::chrono::milliseconds(1000)) {}
  void startSubscription(const std::set<std::string>& cluster_names) override {
    expectEnableInitFetchTimeoutTimer(std::chrono::milliseconds(1000));
    expectCreateEnablePeriodicStatsTimer(
        std::chrono::milliseconds(PERIODIC_STATS_TIMER_REFRESH_MS));
    test_harness_->startSubscription(cluster_names);
  }
};

SubscriptionType types[] = {SubscriptionType::Grpc, SubscriptionType::DeltaGrpc,
                            SubscriptionType::Http, SubscriptionType::Filesystem};
INSTANTIATE_TEST_SUITE_P(SubscriptionImplTest, SubscriptionImplTest, testing::ValuesIn(types));
INSTANTIATE_TEST_SUITE_P(SubscriptionImplTest, SubscriptionImplInitFetchTimeoutTest,
                         testing::ValuesIn(types));

// Validate basic request-response succeeds.
TEST_P(SubscriptionImplTest, InitialRequestResponse) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "v25-ubuntu18-beta", true);
  EXPECT_TRUE(
      statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 18202868392629624077U, "v25-ubuntu18-beta", 0));
  expectDisablePeriodicStatsTimer();
}

// Validate that multiple streamed updates succeed.
TEST_P(SubscriptionImplTest, ResponseStream) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "1.2.3.4", true);
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 14026795738668939420U, "1.2.3.4", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "5_6_7", true);
  EXPECT_TRUE(statsAre(3, 2, 0, 0, 0, TEST_TIME_MILLIS, 7612520132475921171U, "5_6_7", 0));
  expectDisablePeriodicStatsTimer();
}

// Validate that the client can reject a config.
TEST_P(SubscriptionImplTest, RejectConfig) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, "", 0));
  expectDisablePeriodicStatsTimer();
}

// Validate that the client can reject a config and accept the same config later.
TEST_P(SubscriptionImplTest, RejectAcceptConfig) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 1, 1, 0, 0, TEST_TIME_MILLIS, 7148434200721666028, "0", 0));
  expectDisablePeriodicStatsTimer();
}

// Validate that the client can reject a config and accept another config later.
TEST_P(SubscriptionImplTest, RejectAcceptNextConfig) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
  EXPECT_TRUE(statsAre(3, 1, 1, 0, 0, TEST_TIME_MILLIS, 13237225503670494420U, "1", 0));
  expectDisablePeriodicStatsTimer();
}

// Validate that stream updates send a message with the updated resources.
TEST_P(SubscriptionImplTest, UpdateResources) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "42", true);
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 7919287270473417401, "42", 0));
  updateResourceInterest({"cluster2"});
  EXPECT_TRUE(statsAre(3, 1, 0, 0, 0, TEST_TIME_MILLIS, 7919287270473417401, "42", 0));
  expectDisablePeriodicStatsTimer();
}

TEST_P(SubscriptionImplTest, SuccessfulUpdateTimeSinceLastUpdateStat) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "42", true);
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 7919287270473417401, "42", 0));
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 10)));
  expectEnablePeriodicStatsTimer(std::chrono::milliseconds(PERIODIC_STATS_TIMER_REFRESH_MS));
  callPeriodicStatsTimerCb();
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 7919287270473417401, "42", 10));
  expectDisablePeriodicStatsTimer();
}

TEST_P(SubscriptionImplTest, RejectUpdateTimeSinceLastUpdateStat) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "42", false);
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, "", 0));
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 10)));
  expectEnablePeriodicStatsTimer(std::chrono::milliseconds(PERIODIC_STATS_TIMER_REFRESH_MS));
  callPeriodicStatsTimerCb();
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, "", 10));
  expectDisablePeriodicStatsTimer();
}

TEST_P(SubscriptionImplTest, RejectAcceptNextConfigUpdateTimeSinceLastUpdateStat) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, "", 0));
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 10)));
  expectEnablePeriodicStatsTimer(std::chrono::milliseconds(PERIODIC_STATS_TIMER_REFRESH_MS));
  callPeriodicStatsTimerCb();
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, "", 10));
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 20)));
  expectEnablePeriodicStatsTimer(std::chrono::milliseconds(PERIODIC_STATS_TIMER_REFRESH_MS));
  callPeriodicStatsTimerCb();
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, "", 20));
  deliverConfigUpdate({"cluster0", "cluster1"}, "1", true);
  EXPECT_TRUE(statsAre(3, 1, 1, 0, 0, TEST_TIME_MILLIS + 20, 13237225503670494420U, "1", 0));
  expectDisablePeriodicStatsTimer();
}

TEST_P(SubscriptionImplTest, SuccessfulUpdateThenWaitTimeSinceLastUpdateStat) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "42", true);
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 7919287270473417401, "42", 0));
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 10)));
  expectEnablePeriodicStatsTimer(std::chrono::milliseconds(PERIODIC_STATS_TIMER_REFRESH_MS));
  callPeriodicStatsTimerCb();
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 25)));
  expectEnablePeriodicStatsTimer(std::chrono::milliseconds(PERIODIC_STATS_TIMER_REFRESH_MS));
  callPeriodicStatsTimerCb();
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 7919287270473417401, "42", 25));
  expectDisablePeriodicStatsTimer();
}

// Validate that initial fetch timer is created and calls callback on timeout
TEST_P(SubscriptionImplInitFetchTimeoutTest, InitialFetchTimeout) {
  if (GetParam() == SubscriptionType::Filesystem) {
    return; // initial_fetch_timeout not implemented for filesystem.
  }
  InSequence s;
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));

  expectDisableInitFetchTimeoutTimer();

  expectConfigUpdateFailed();

  callInitFetchTimeoutCb();
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 1, 0, 0, "", 0));
  expectDisablePeriodicStatsTimer();
}

// Validate that initial fetch timer is disabled on config update
TEST_P(SubscriptionImplInitFetchTimeoutTest, DisableInitTimeoutOnSuccess) {
  InSequence s;
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  expectDisableInitFetchTimeoutTimer();
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  expectDisablePeriodicStatsTimer();
}

// Validate that initial fetch timer is disabled on config update failed
TEST_P(SubscriptionImplInitFetchTimeoutTest, DisableInitTimeoutOnFail) {
  InSequence s;
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, "", 0));
  expectDisableInitFetchTimeoutTimer();
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  expectDisablePeriodicStatsTimer();
}

} // namespace
} // namespace Config
} // namespace Envoy
