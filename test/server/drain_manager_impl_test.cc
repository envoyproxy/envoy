#include <chrono>

#include "envoy/config/listener/v3/listener.pb.h"

#include "server/drain_manager_impl.h"

#include "test/mocks/server/instance.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Server {
namespace {

constexpr int DrainTimeSeconds(600);

class DrainManagerImplTest : public Event::TestUsingSimulatedTime,
                             public testing::TestWithParam<bool> {
protected:
  DrainManagerImplTest() {
    ON_CALL(server_.options_, drainTime())
        .WillByDefault(Return(std::chrono::seconds(DrainTimeSeconds)));
    ON_CALL(server_.options_, parentShutdownTime())
        .WillByDefault(Return(std::chrono::seconds(900)));
  }

  NiceMock<MockInstance> server_;
};

TEST_F(DrainManagerImplTest, Default) {
  InSequence s;
  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT);

  // Test parent shutdown.
  Event::MockTimer* shutdown_timer = new Event::MockTimer(&server_.dispatcher_);
  EXPECT_CALL(*shutdown_timer, enableTimer(std::chrono::milliseconds(900000), _));
  drain_manager.startParentShutdownSequence();

  EXPECT_CALL(server_.hot_restart_, sendParentTerminateRequest());
  shutdown_timer->invokeCallback();

  // Verify basic drain close.
  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(false));
  EXPECT_FALSE(drain_manager.drainClose());
  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_TRUE(drain_manager.drainClose());

  // Test drain sequence.
  Event::MockTimer* drain_timer = new Event::MockTimer(&server_.dispatcher_);
  const auto expected_delay = std::chrono::milliseconds(DrainTimeSeconds * 1000);
  EXPECT_CALL(*drain_timer, enableTimer(expected_delay, nullptr));
  ReadyWatcher drain_complete;
  drain_manager.startDrainSequence([&drain_complete]() -> void { drain_complete.ready(); });
  EXPECT_CALL(drain_complete, ready());
  drain_timer->invokeCallback();
}

TEST_F(DrainManagerImplTest, ModifyOnly) {
  InSequence s;
  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::MODIFY_ONLY);

  EXPECT_CALL(server_, healthCheckFailed()).Times(0); // Listener check will short-circuit
  EXPECT_FALSE(drain_manager.drainClose());
}

TEST_P(DrainManagerImplTest, DrainDeadline) {
  const bool drain_gradually = GetParam();
  ON_CALL(server_.options_, drainStrategy())
      .WillByDefault(Return(drain_gradually ? Server::DrainStrategy::Gradual
                                            : Server::DrainStrategy::Immediate));
  // TODO(auni53): Add integration tests for this once TestDrainManager is
  // removed.
  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT);

  // Ensure drainClose() behaviour is determined by the deadline.
  drain_manager.startDrainSequence([] {});
  EXPECT_CALL(server_, healthCheckFailed()).WillRepeatedly(Return(false));
  ON_CALL(server_.api_.random_, random()).WillByDefault(Return(DrainTimeSeconds * 2 - 1));
  ON_CALL(server_.options_, drainTime())
      .WillByDefault(Return(std::chrono::seconds(DrainTimeSeconds)));

  if (drain_gradually) {
    // random() should be called when elapsed time < drain timeout
    EXPECT_CALL(server_.api_.random_, random()).Times(2);
    EXPECT_FALSE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(DrainTimeSeconds - 1));
    EXPECT_FALSE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(1));
    EXPECT_TRUE(drain_manager.drainClose());

    // Test that this still works if remaining time is negative
    simTime().advanceTimeWait(std::chrono::seconds(1));
    EXPECT_TRUE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(500));
    EXPECT_TRUE(drain_manager.drainClose());
  } else {
    EXPECT_CALL(server_.api_.random_, random()).Times(0);
    EXPECT_TRUE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(DrainTimeSeconds - 1));
    EXPECT_TRUE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(1));
    EXPECT_TRUE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(1));
    EXPECT_TRUE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(500));
    EXPECT_TRUE(drain_manager.drainClose());
  }
}

TEST_P(DrainManagerImplTest, DrainDeadlineProbability) {
  const bool drain_gradually = GetParam();
  ON_CALL(server_.options_, drainStrategy())
      .WillByDefault(Return(drain_gradually ? Server::DrainStrategy::Gradual
                                            : Server::DrainStrategy::Immediate));
  ON_CALL(server_.api_.random_, random()).WillByDefault(Return(4));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(3)));

  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT);

  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_TRUE(drain_manager.drainClose());
  EXPECT_CALL(server_, healthCheckFailed()).WillRepeatedly(Return(false));
  EXPECT_FALSE(drain_manager.drainClose());
  EXPECT_FALSE(drain_manager.draining());

  drain_manager.startDrainSequence([] {});
  EXPECT_TRUE(drain_manager.draining());

  if (drain_gradually) {
    // random() should be called when elapsed time < drain timeout
    EXPECT_CALL(server_.api_.random_, random()).Times(2);
    // Current elapsed time is 0
    // drainClose() will return true when elapsed time > (4 % 3 == 1).
    EXPECT_FALSE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(2));
    EXPECT_TRUE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(1));
    EXPECT_TRUE(drain_manager.drainClose());
  } else {
    EXPECT_CALL(server_.api_.random_, random()).Times(0);
    EXPECT_TRUE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(2));
    EXPECT_TRUE(drain_manager.drainClose());
    simTime().advanceTimeWait(std::chrono::seconds(1));
    EXPECT_TRUE(drain_manager.drainClose());
  }
}

INSTANTIATE_TEST_SUITE_P(DrainStrategies, DrainManagerImplTest, testing::Bool());

} // namespace
} // namespace Server
} // namespace Envoy
