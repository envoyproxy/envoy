#include <chrono>

#include "server/drain_manager_impl.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Lyft {
using testing::_;
using testing::Return;
using testing::SaveArg;

namespace Server {

TEST(DrainManagerImplTest, All) {
  NiceMock<MockInstance> server;
  ON_CALL(server.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(600)));
  ON_CALL(server.options_, parentShutdownTime()).WillByDefault(Return(std::chrono::seconds(900)));
  DrainManagerImpl drain_manager(server);

  // Test parent shutdown.
  Event::MockTimer* shutdown_timer = new Event::MockTimer(&server.dispatcher_);
  EXPECT_CALL(*shutdown_timer, enableTimer(std::chrono::milliseconds(900000)));
  drain_manager.startParentShutdownSequence();

  EXPECT_CALL(server.hot_restart_, terminateParent());
  shutdown_timer->callback_();

  // Verify basic drain close.
  EXPECT_CALL(server, healthCheckFailed()).WillOnce(Return(false));
  EXPECT_FALSE(drain_manager.drainClose());
  EXPECT_CALL(server, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_TRUE(drain_manager.drainClose());

  // Test drain sequence.
  Event::MockTimer* drain_timer = new Event::MockTimer(&server.dispatcher_);
  EXPECT_CALL(*drain_timer, enableTimer(_));
  drain_manager.startDrainSequence();

  // 600s which is the default drain time.
  for (size_t i = 0; i < 599; i++) {
    if (i < 598) {
      EXPECT_CALL(*drain_timer, enableTimer(_));
    }
    drain_timer->callback_();
  }

  EXPECT_CALL(server, healthCheckFailed()).WillOnce(Return(false));
  EXPECT_TRUE(drain_manager.drainClose());
}

} // Server
} // Lyft