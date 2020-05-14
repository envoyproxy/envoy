#include <chrono>

#include "envoy/config/listener/v3/listener.pb.h"

#include "server/drain_manager_impl.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Server {
namespace {

const int kDrainTimeSeconds(600);

class DrainManagerImplTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  DrainManagerImplTest() {
    ON_CALL(server_.options_, drainTime())
        .WillByDefault(Return(std::chrono::seconds(kDrainTimeSeconds)));
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
  EXPECT_CALL(*drain_timer, enableTimer(_, _)); // TODO(auni) add arg
  ReadyWatcher drain_complete;
  drain_manager.startDrainSequence([&drain_complete]() -> void { drain_complete.ready(); });
  EXPECT_CALL(drain_complete, ready());
  drain_timer->invokeCallback();
}

TEST_F(DrainManagerImplTest, DrainDeadline) {
  // TODO(auni53): Add integration tests for this once TestDrainManager is
  // removed.
  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT);

  // Ensure drainClose() behaviour is determined by the deadline.
  drain_manager.startDrainSequence([] {});
  EXPECT_CALL(server_, healthCheckFailed()).WillRepeatedly(Return(false));
  ON_CALL(server_.random_, random()).WillByDefault(Return(kDrainTimeSeconds * 2 - 1));
  ON_CALL(server_.options_, drainTime())
      .WillByDefault(Return(std::chrono::seconds(kDrainTimeSeconds)));

  // random() should be called when elapsed time < drain timeout
  EXPECT_CALL(server_.random_, random()).Times(2);
  EXPECT_FALSE(drain_manager.drainClose());
  simTime().advanceTimeWait(std::chrono::seconds(kDrainTimeSeconds - 1));
  EXPECT_FALSE(drain_manager.drainClose());
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_TRUE(drain_manager.drainClose());

  // Test that this still works if remaining time is negative
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_TRUE(drain_manager.drainClose());
  simTime().advanceTimeWait(std::chrono::seconds(500));
  EXPECT_TRUE(drain_manager.drainClose());
}

TEST_F(DrainManagerImplTest, DrainDeadlineProbability) {
  ON_CALL(server_.random_, random()).WillByDefault(Return(4));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(3)));

  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT);

  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_TRUE(drain_manager.drainClose());
  EXPECT_CALL(server_, healthCheckFailed()).WillRepeatedly(Return(false));
  EXPECT_FALSE(drain_manager.drainClose());
  drain_manager.startDrainSequence([] {});

  // random() should be called when elapsed time < drain timeout
  EXPECT_CALL(server_.random_, random()).Times(2);
  // Current elapsed time is 0
  // drainClose() will return true when elapsed time > (4 % 3 == 1).
  EXPECT_FALSE(drain_manager.drainClose());
  simTime().advanceTimeWait(std::chrono::seconds(2));
  EXPECT_TRUE(drain_manager.drainClose());
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_TRUE(drain_manager.drainClose());
}

TEST_F(DrainManagerImplTest, ModifyOnly) {
  InSequence s;
  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::MODIFY_ONLY);

  EXPECT_CALL(server_, healthCheckFailed()).Times(0); // Listener check will short-circuit
  EXPECT_FALSE(drain_manager.drainClose());
}

} // namespace
} // namespace Server
} // namespace Envoy
