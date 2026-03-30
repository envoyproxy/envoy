#include "source/common/upstream/health_checker_group.h"

#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::_;
using ::testing::NiceMock;

class HealthCheckerGroupTest : public testing::Test {
protected:
  void SetUp() override {
    host_ = std::make_shared<NiceMock<MockHostLight>>();
    // Use real flag storage for health flag operations.
    ON_CALL(*host_, healthFlagGet(_)).WillByDefault([this](Host::HealthFlag flag) -> bool {
      return health_flags_ & static_cast<uint32_t>(flag);
    });
    ON_CALL(*host_, healthFlagSet(_)).WillByDefault([this](Host::HealthFlag flag) {
      health_flags_ |= static_cast<uint32_t>(flag);
    });
    ON_CALL(*host_, healthFlagClear(_)).WillByDefault([this](Host::HealthFlag flag) {
      health_flags_ &= ~static_cast<uint32_t>(flag);
    });

    // Start with FAILED_ACTIVE_HC and PENDING_ACTIVE_HC set (new host state).
    health_flags_ = static_cast<uint32_t>(Host::HealthFlag::FAILED_ACTIVE_HC) |
                    static_cast<uint32_t>(Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  // Helper to create a group with two checkers and register a tracking callback.
  void setupTwoCheckerGroup() {
    checker_a_ = std::make_shared<NiceMock<MockHealthChecker>>();
    checker_b_ = std::make_shared<NiceMock<MockHealthChecker>>();
    group_.addChecker(checker_a_);
    group_.addChecker(checker_b_);
    group_.addHostCheckCompleteCb(
        [this](const HostSharedPtr&, HealthTransition transition, HealthState state) {
          last_transition_ = transition;
          last_state_ = state;
          callback_count_++;
        });
  }

  uint32_t health_flags_{0};
  std::shared_ptr<NiceMock<MockHostLight>> host_;
  std::shared_ptr<NiceMock<MockHealthChecker>> checker_a_;
  std::shared_ptr<NiceMock<MockHealthChecker>> checker_b_;
  HealthCheckerGroup group_;
  HealthTransition last_transition_ = HealthTransition::Unchanged;
  HealthState last_state_ = HealthState::Unhealthy;
  int callback_count_ = 0;
};

// Verify that outer callbacks are suppressed until all checkers respond for a host.
// This ensures the cluster's init counter sees exactly one callback per host.
TEST_F(HealthCheckerGroupTest, CallbacksSuppressedDuringInit) {
  setupTwoCheckerGroup();

  // Checker A responds. Outer callback should NOT fire yet (B hasn't responded).
  checker_a_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  EXPECT_EQ(0, callback_count_);
  // Host is still pending (B hasn't responded) and failed (B still failing).
  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  // Checker B responds. Now outer callback fires (both have responded).
  checker_b_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  EXPECT_EQ(1, callback_count_);
  // Now host is healthy and no longer pending.
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(HealthTransition::Changed, last_transition_);
  EXPECT_EQ(HealthState::Healthy, last_state_);
}

// Verify that with two checkers, the host stays unhealthy until both pass.
TEST_F(HealthCheckerGroupTest, BothCheckersMustPass) {
  setupTwoCheckerGroup();

  // A passes, B fails. Both have responded but B is unhealthy.
  checker_a_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  checker_b_->runCallbacks(host_, HealthTransition::Changed, HealthState::Unhealthy);
  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  // Callback fired (both responded), host is still unhealthy.
  EXPECT_EQ(1, callback_count_);

  // B now passes. Host becomes healthy.
  checker_b_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(2, callback_count_);
  EXPECT_EQ(HealthTransition::Changed, last_transition_);
  EXPECT_EQ(HealthState::Healthy, last_state_);
}

// Verify that if one checker fails after both passed, host becomes unhealthy.
TEST_F(HealthCheckerGroupTest, OneFailureMakesUnhealthy) {
  setupTwoCheckerGroup();

  // Both pass -> healthy.
  checker_a_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  checker_b_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  // Checker A fails -> unhealthy.
  callback_count_ = 0;
  checker_a_->runCallbacks(host_, HealthTransition::Changed, HealthState::Unhealthy);
  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(1, callback_count_);
  EXPECT_EQ(HealthTransition::Changed, last_transition_);
  EXPECT_EQ(HealthState::Unhealthy, last_state_);
}

// Verify that a single checker (group of 1) behaves identically to a standalone checker.
TEST_F(HealthCheckerGroupTest, SingleCheckerPassthrough) {
  auto checker = std::make_shared<NiceMock<MockHealthChecker>>();
  group_.addChecker(checker);
  group_.addHostCheckCompleteCb(
      [this](const HostSharedPtr&, HealthTransition transition, HealthState state) {
        last_transition_ = transition;
        last_state_ = state;
        callback_count_++;
      });

  // Checker reports healthy -> aggregate changes.
  checker->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(1, callback_count_);
  EXPECT_EQ(HealthTransition::Changed, last_transition_);
  EXPECT_EQ(HealthState::Healthy, last_state_);

  // Checker reports unhealthy -> aggregate changes.
  checker->runCallbacks(host_, HealthTransition::Changed, HealthState::Unhealthy);
  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(2, callback_count_);
  EXPECT_EQ(HealthTransition::Changed, last_transition_);
  EXPECT_EQ(HealthState::Unhealthy, last_state_);
}

// Verify that Unchanged transitions are forwarded after init.
TEST_F(HealthCheckerGroupTest, UnchangedTransitionAfterInit) {
  setupTwoCheckerGroup();

  // Both pass first (completes init).
  checker_a_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  checker_b_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  callback_count_ = 0;

  // Unchanged from checker A -> forwarded as Unchanged.
  checker_a_->runCallbacks(host_, HealthTransition::Unchanged, HealthState::Healthy);
  EXPECT_EQ(1, callback_count_);
  EXPECT_EQ(HealthTransition::Unchanged, last_transition_);
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
}

// Verify pending bit tracking.
TEST_F(HealthCheckerGroupTest, PendingFlagClearedWhenAllCheckersRespond) {
  setupTwoCheckerGroup();

  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));

  // Checker A responds -> still pending (B hasn't responded).
  checker_a_->runCallbacks(host_, HealthTransition::Unchanged, HealthState::Unhealthy);
  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));

  // Checker B responds -> no longer pending.
  checker_b_->runCallbacks(host_, HealthTransition::Unchanged, HealthState::Unhealthy);
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
}

// Verify start() calls start on all inner checkers.
TEST_F(HealthCheckerGroupTest, StartCallsAllCheckers) {
  auto checker_a = std::make_shared<NiceMock<MockHealthChecker>>();
  auto checker_b = std::make_shared<NiceMock<MockHealthChecker>>();

  EXPECT_CALL(*checker_a, start());
  EXPECT_CALL(*checker_b, start());

  group_.addChecker(checker_a);
  group_.addChecker(checker_b);
  group_.start();
}

// Verify numCheckers returns correct count.
TEST_F(HealthCheckerGroupTest, NumCheckers) {
  EXPECT_EQ(0u, group_.numCheckers());

  group_.addChecker(std::make_shared<NiceMock<MockHealthChecker>>());
  EXPECT_EQ(1u, group_.numCheckers());

  group_.addChecker(std::make_shared<NiceMock<MockHealthChecker>>());
  EXPECT_EQ(2u, group_.numCheckers());
}

// Verify recovery: A fails, B healthy, then A recovers -> host becomes healthy.
TEST_F(HealthCheckerGroupTest, RecoveryAfterPartialFailure) {
  setupTwoCheckerGroup();

  // Both pass (completes init).
  checker_a_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  checker_b_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  // A fails.
  checker_a_->runCallbacks(host_, HealthTransition::Changed, HealthState::Unhealthy);
  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  // B still reports healthy (unchanged). Host stays unhealthy.
  checker_b_->runCallbacks(host_, HealthTransition::Unchanged, HealthState::Healthy);
  EXPECT_TRUE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  // A recovers.
  callback_count_ = 0;
  checker_a_->runCallbacks(host_, HealthTransition::Changed, HealthState::Healthy);
  EXPECT_FALSE(host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(1, callback_count_);
  EXPECT_EQ(HealthTransition::Changed, last_transition_);
  EXPECT_EQ(HealthState::Healthy, last_state_);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
