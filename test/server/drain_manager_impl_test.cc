#include "server/drain_manager_impl.h"

#include "test/mocks/server/mocks.h"

using testing::_;
using testing::Return;
using testing::SaveArg;

namespace Server {

TEST(DrainManagerImplTest, All) {
  NiceMock<MockInstance> server;
  DrainManagerImpl drain_manager(server);

  // Test parent shutdown.
  Event::MockTimer* shutdown_timer = new Event::MockTimer(&server.dispatcher_);
  EXPECT_CALL(*shutdown_timer, enableTimer(_));
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
