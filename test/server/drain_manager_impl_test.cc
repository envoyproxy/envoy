#include <chrono>

#include "server/drain_manager_impl.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Server {

class DrainManagerImplTest : public testing::Test {
public:
  DrainManagerImplTest() {
    ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(600)));
    ON_CALL(server_.options_, parentShutdownTime())
        .WillByDefault(Return(std::chrono::seconds(900)));
  }

  NiceMock<MockInstance> server_;
};

TEST_F(DrainManagerImplTest, Default) {
  InSequence s;
  DrainManagerImpl drain_manager(server_, envoy::api::v2::Listener_DrainType_DEFAULT);

  // Test parent shutdown.
  Event::MockTimer* shutdown_timer = new Event::MockTimer(&server_.dispatcher_);
  EXPECT_CALL(*shutdown_timer, enableTimer(std::chrono::milliseconds(900000)));
  drain_manager.startParentShutdownSequence();

  EXPECT_CALL(server_.hot_restart_, terminateParent());
  shutdown_timer->callback_();

  // Verify basic drain close.
  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(false));
  EXPECT_FALSE(drain_manager.drainClose());
  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_TRUE(drain_manager.drainClose());

  // Test drain sequence.
  Event::MockTimer* drain_timer = new Event::MockTimer(&server_.dispatcher_);
  EXPECT_CALL(*drain_timer, enableTimer(_));
  ReadyWatcher drain_complete;
  drain_manager.startDrainSequence([&drain_complete]() -> void { drain_complete.ready(); });

  // 600s which is the default drain time.
  for (size_t i = 0; i < 599; i++) {
    if (i < 598) {
      EXPECT_CALL(*drain_timer, enableTimer(_));
    } else {
      EXPECT_CALL(drain_complete, ready());
    }
    drain_timer->callback_();
  }

  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(false));
  EXPECT_TRUE(drain_manager.drainClose());
}

TEST_F(DrainManagerImplTest, ModifyOnly) {
  InSequence s;
  DrainManagerImpl drain_manager(server_, envoy::api::v2::Listener_DrainType_MODIFY_ONLY);

  EXPECT_CALL(server_, healthCheckFailed()).Times(0);
  EXPECT_FALSE(drain_manager.drainClose());
}

} // namespace Server
} // namespace Envoy
