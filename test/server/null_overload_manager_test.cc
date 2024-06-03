#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/server/overload/thread_local_overload_state.h"

#include "source/server/null_overload_manager.h"
#include "source/server/overload_manager_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::FloatNear;
using testing::NiceMock;
using testing::Property;

namespace Envoy {
namespace Server {
namespace {

class NullOverloadManagerTest : public testing::Test {
protected:
  NullOverloadManagerTest() {}

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
};

TEST_F(NullOverloadManagerTest, NullOverloadManagerOverloadState) {
  NullOverloadManager::OverloadState overload_state_non_permissive(dispatcher_, false);

  // Verify that the manager always returns an inactive state for any action
  auto action_state = overload_state_non_permissive.getState("envoy.overload_actions.shrink_heap");
  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, UnitFloat::min())));
  action_state =
      overload_state_non_permissive.getState("envoy.overload_actions.stop_accepting_requests");
  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, UnitFloat::min())));
  action_state = overload_state_non_permissive.getState("envoy.overload_actions.some_other_action");
  EXPECT_THAT(action_state, AllOf(Property(&OverloadActionState::isSaturated, false),
                                  Property(&OverloadActionState::value, UnitFloat::min())));

  // Verify that the manager does not allocate or deallocate any resources
  EXPECT_FALSE(overload_state_non_permissive.tryAllocateResource(
      Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections, 100));
  EXPECT_FALSE(overload_state_non_permissive.tryDeallocateResource(
      Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections, 50));

  // Verify that the manager does not enable any resource monitors
  EXPECT_FALSE(overload_state_non_permissive.isResourceMonitorEnabled(
      Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections));

  // Verify that the manager returns a null ProactiveResourceMonitor for any resource
  EXPECT_FALSE(overload_state_non_permissive
                   .getProactiveResourceMonitorForTest(
                       Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections)
                   .has_value());
}

TEST_F(NullOverloadManagerTest, NullOverloadManagerNoOpOperations) {
  NullOverloadManager overload_manager(thread_local_, false);
  EXPECT_TRUE(overload_manager.registerForAction("envoy.overload_actions.shrink_heap", dispatcher_,
                                                 [](OverloadActionState) {}));
  auto* point = overload_manager.getLoadShedPoint("envoy.load_shed_point.dummy_point");
  EXPECT_EQ(point, nullptr);
  auto scaled_timer_factory = overload_manager.scaledTimerFactory();
  EXPECT_EQ(scaled_timer_factory, nullptr);
}

} // namespace
} // namespace Server
} // namespace Envoy
