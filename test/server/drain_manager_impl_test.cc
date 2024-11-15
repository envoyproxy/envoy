#include <chrono>

#include "envoy/common/callback.h"
#include "envoy/config/listener/v3/listener.pb.h"

#include "source/server/drain_manager_impl.h"

#include "test/mocks/server/instance.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::Ge;
using testing::InSequence;
using testing::Le;
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

  template <class Duration>
  void testRegisterCallbackAfterDrainBeginGradualStrategy(Duration delay) {
    ON_CALL(server_.options_, drainStrategy())
        .WillByDefault(Return(Server::DrainStrategy::Gradual));
    ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

    DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT,
                                   server_.dispatcher());

    testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_before_drain;
    testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_after_drain1;
    testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_after_drain2;

    EXPECT_CALL(cb_before_drain, Call(_));
    // Validate that callbacks after the drain sequence has started (or after the drain deadline
    // has been reached) are called with a random value between 0 (immediate) and the max
    // drain window (minus time that has passed).
    EXPECT_CALL(cb_after_drain1, Call(_)).WillOnce(Invoke([](std::chrono::milliseconds delay) {
      EXPECT_THAT(delay.count(), Ge(0));
      EXPECT_THAT(delay.count(), Le(990));
      return absl::OkStatus();
    }));
    EXPECT_CALL(cb_after_drain2, Call(_)).WillOnce(Invoke([](std::chrono::milliseconds delay) {
      EXPECT_EQ(delay.count(), 0);
      return absl::OkStatus();
    }));

    auto before_handle = drain_manager.addOnDrainCloseCb(cb_before_drain.AsStdFunction());
    drain_manager.startDrainSequence([] {});

    server_.api_.time_system_.advanceTimeWait(std::chrono::milliseconds(10));
    auto after_handle1 = drain_manager.addOnDrainCloseCb(cb_after_drain1.AsStdFunction());

    server_.api_.time_system_.advanceTimeWait(delay);
    auto after_handle2 = drain_manager.addOnDrainCloseCb(cb_after_drain2.AsStdFunction());

    EXPECT_EQ(after_handle1, nullptr);
    EXPECT_EQ(after_handle2, nullptr);
  }

  NiceMock<MockInstance> server_;
};

TEST_F(DrainManagerImplTest, Default) {
  InSequence s;
  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT,
                                 server_.dispatcher());

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
  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::MODIFY_ONLY,
                                 server_.dispatcher());

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
  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT,
                                 server_.dispatcher());

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

  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT,
                                 server_.dispatcher());

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

TEST_P(DrainManagerImplTest, OnDrainCallbacks) {
  constexpr int num_cbs = 20;
  const bool drain_gradually = GetParam();
  ON_CALL(server_.options_, drainStrategy())
      .WillByDefault(Return(drain_gradually ? Server::DrainStrategy::Gradual
                                            : Server::DrainStrategy::Immediate));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(4)));

  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT,
                                 server_.dispatcher());

  {
    // Register callbacks (store in array to keep in scope for test)
    std::array<testing::MockFunction<absl::Status(std::chrono::milliseconds)>, num_cbs> cbs;
    std::array<Common::CallbackHandlePtr, num_cbs> cb_handles;
    for (auto i = 0; i < num_cbs; i++) {
      auto& cb = cbs[i];
      if (drain_gradually) {
        auto step = 1000 / num_cbs;
        EXPECT_CALL(cb, Call(_)).WillRepeatedly(Invoke([i, step](std::chrono::milliseconds delay) {
          // Everything should happen within the first 1/4 of the drain time
          EXPECT_LT(delay.count(), 1001);

          // Validate that our wait times are spread out (within some small error)
          EXPECT_THAT(delay.count(), AllOf(Ge(i * step - 1), Le(i * step + 1)));
          return absl::OkStatus();
        }));
      } else {
        EXPECT_CALL(cb, Call(std::chrono::milliseconds{0}));
      }

      cb_handles[i] = drain_manager.addOnDrainCloseCb(cb.AsStdFunction());
    }
    drain_manager.startDrainSequence([] {});
  }

  EXPECT_TRUE(drain_manager.draining());
}

INSTANTIATE_TEST_SUITE_P(DrainStrategies, DrainManagerImplTest, testing::Bool());

// Test gradual draining when there are more callbacks than milliseconds in the drain time,
// which should cause some drains to happen within roughly the same window.
TEST_F(DrainManagerImplTest, OnDrainCallbacksManyGradualSteps) {
  constexpr int num_cbs = 3000;
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(4)));

  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT,
                                 server_.dispatcher());

  {
    // Register callbacks (store in array to keep in scope for test)
    std::array<testing::MockFunction<absl::Status(std::chrono::milliseconds)>, num_cbs> cbs;
    std::array<Common::CallbackHandlePtr, num_cbs> cb_handles;
    for (auto i = 0; i < num_cbs; i++) {
      auto& cb = cbs[i];
      auto step = 1000.0 / num_cbs;
      EXPECT_CALL(cb, Call(_)).WillRepeatedly(Invoke([i, step](std::chrono::milliseconds delay) {
        // Everything should happen within the first 1/4 of the drain time
        EXPECT_LT(delay.count(), 1001);

        // Validate that our wait times are spread out (within some small error)
        EXPECT_THAT(delay.count(), AllOf(Ge(i * step - 1), Le(i * step + 1)));
        return absl::OkStatus();
      }));

      cb_handles[i] = drain_manager.addOnDrainCloseCb(cb.AsStdFunction());
    }
    drain_manager.startDrainSequence([] {});
  }

  EXPECT_TRUE(drain_manager.draining());
}

// Test gradual draining when the number of callbacks does not evenly divide into
// the drain time.
TEST_F(DrainManagerImplTest, OnDrainCallbacksNonEvenlyDividedSteps) {
  constexpr int num_cbs = 30;
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT,
                                 server_.dispatcher());

  {
    // Register callbacks (store in array to keep in scope for test)
    std::array<testing::MockFunction<absl::Status(std::chrono::milliseconds)>, num_cbs> cbs;
    std::array<Common::CallbackHandlePtr, num_cbs> cb_handles;
    for (auto i = 0; i < num_cbs; i++) {
      auto& cb = cbs[i];
      auto step = 250.0 / num_cbs;
      EXPECT_CALL(cb, Call(_)).WillRepeatedly(Invoke([i, step](std::chrono::milliseconds delay) {
        // Everything should happen within the first 1/4 of the drain time
        EXPECT_LT(delay.count(), 251);

        // Validate that our wait times are spread out (within some small error)
        EXPECT_THAT(delay.count(), AllOf(Ge(i * step - 1), Le(i * step + 1)));
        return absl::OkStatus();
      }));

      cb_handles[i] = drain_manager.addOnDrainCloseCb(cb.AsStdFunction());
    }

    drain_manager.startDrainSequence([] {});
  }

  EXPECT_TRUE(drain_manager.draining());
}

// Validate the expected behavior when a drain-close callback is registered
// after draining has begun with a Gradual drain strategy (should be called with
// delay between 0 and maximum)
TEST_F(DrainManagerImplTest, RegisterCallbackAfterDrainBeginGradualStrategy) {
  testRegisterCallbackAfterDrainBeginGradualStrategy(std::chrono::milliseconds(1000));
}

// Repeat above test, but add simulated delay that falls 1 microsecond short of
// the deadline, thus triggering a corner case where the current time is less
// than the deadline by 1 microsecond, which rounds to 0 milliseconds.
TEST_F(DrainManagerImplTest, RegisterCallbackAfterDrainBeginGradualStrategyMicroDelay) {
  testRegisterCallbackAfterDrainBeginGradualStrategy(std::chrono::microseconds(990 * 1000 - 1));
}

// Validate the expected behavior when a drain-close callback is registered after draining has begun
// with an Immediate drain strategy (should be called with 0 delay)
TEST_F(DrainManagerImplTest, RegisterCallbackAfterDrainBeginImmediateStrategy) {
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

  DrainManagerImpl drain_manager(server_, envoy::config::listener::v3::Listener::DEFAULT,
                                 server_.dispatcher());

  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_before_drain;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_after_drain;

  EXPECT_CALL(cb_before_drain, Call(_));
  EXPECT_CALL(cb_after_drain, Call(_)).WillOnce(Invoke([](std::chrono::milliseconds delay) {
    EXPECT_EQ(delay.count(), 0);
    return absl::OkStatus();
  }));

  auto before_handle = drain_manager.addOnDrainCloseCb(cb_before_drain.AsStdFunction());
  drain_manager.startDrainSequence([] {});
  auto after_handle = drain_manager.addOnDrainCloseCb(cb_after_drain.AsStdFunction());
  EXPECT_EQ(after_handle, nullptr);
}

// Destruction doesn't trigger draining, so it should be for the parent to be cleaned up
// before the child.
TEST_F(DrainManagerImplTest, ParentDestructedBeforeChildren) {
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

  auto parent = std::make_unique<DrainManagerImpl>(
      server_, envoy::config::listener::v3::Listener::DEFAULT, server_.dispatcher());
  auto child_a = parent->createChildManager(server_.dispatcher());
  auto child_b = parent->createChildManager(server_.dispatcher());

  EXPECT_FALSE(parent->draining());
  EXPECT_FALSE(child_a->draining());
  EXPECT_FALSE(child_b->draining());

  parent.reset();

  // parent destruction should not effect drain state
  EXPECT_FALSE(child_a->draining());
  EXPECT_FALSE(child_b->draining());

  // Further children creation (from existing children) is still possible
  auto child_a1 = child_a->createChildManager(server_.dispatcher());
  auto child_b1 = child_b->createChildManager(server_.dispatcher());
  EXPECT_TRUE(child_a1 != nullptr);
  EXPECT_TRUE(child_b1 != nullptr);

  // draining cascades as expected
  int called = 0;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_a1;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_b1;
  EXPECT_CALL(cb_a1, Call(_)).WillRepeatedly(Invoke([&called](std::chrono::milliseconds) {
    called += 1;
    return absl::OkStatus();
  }));
  EXPECT_CALL(cb_b1, Call(_)).WillRepeatedly(Invoke([&called](std::chrono::milliseconds) {
    called += 1;
    return absl::OkStatus();
  }));
  auto handle_a1 = child_a1->addOnDrainCloseCb(cb_a1.AsStdFunction());
  auto handle_b1 = child_b1->addOnDrainCloseCb(cb_b1.AsStdFunction());
  child_a->startDrainSequence([] {});
  child_b->startDrainSequence([] {});
  EXPECT_EQ(called, 2);

  // It is safe to clean up children
  child_a.reset();
  child_b.reset();
}

// Validate that draining will cascade through all nodes in the tree. This test uses the following
// tree structure:
//             a
//             │
//      ┌──────┴────────┐
//      ▼               ▼
//      b               c
//      │               │
//  ┌───┴────┐     ┌────┴───┐
//  ▼        ▼     ▼        ▼
//  d        e     f        g
TEST_F(DrainManagerImplTest, DrainingCascadesThroughAllNodesInTree) {
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

  auto a = DrainManagerImpl(server_, envoy::config::listener::v3::Listener::DEFAULT,
                            server_.dispatcher());

  auto b = a.createChildManager(server_.dispatcher());
  auto d = b->createChildManager(server_.dispatcher());
  auto e = b->createChildManager(server_.dispatcher());

  auto c = a.createChildManager(server_.dispatcher());
  auto f = c->createChildManager(server_.dispatcher());
  auto g = c->createChildManager(server_.dispatcher());

  // wire up callbacks at all levels
  int call_count = 0;
  std::array<testing::MockFunction<absl::Status(std::chrono::milliseconds)>, 7> cbs;

  for (auto& cb : cbs) {
    EXPECT_CALL(cb, Call(_)).WillOnce(Invoke([&call_count](std::chrono::milliseconds) {
      call_count++;
      return absl::OkStatus();
    }));
  }
  auto handle_a = a.addOnDrainCloseCb(cbs[0].AsStdFunction());
  auto handle_b = b->addOnDrainCloseCb(cbs[1].AsStdFunction());
  auto handle_c = c->addOnDrainCloseCb(cbs[2].AsStdFunction());
  auto handle_d = d->addOnDrainCloseCb(cbs[3].AsStdFunction());
  auto handle_e = e->addOnDrainCloseCb(cbs[4].AsStdFunction());
  auto handle_f = f->addOnDrainCloseCb(cbs[5].AsStdFunction());
  auto handle_g = g->addOnDrainCloseCb(cbs[6].AsStdFunction());

  a.startDrainSequence([] {});
  EXPECT_EQ(call_count, 7);
}

// Validate that sub-trees are independent of each other (a tree's drain-state is not affected by
// its neighbors). This test uses the following tree structure:
//             a
//             │
//      ┌──────┴────────┐
//      ▼               ▼
//      b               c
//      │               │
//  ┌───┴────┐     ┌────┴───┐
//  ▼        ▼     ▼        ▼
//  d        e     f        g
//
// Draining will happen on B and validate that no impact is seen on C.
TEST_F(DrainManagerImplTest, DrainingIsIndependentToNeighbors) {
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

  auto a = DrainManagerImpl(server_, envoy::config::listener::v3::Listener::DEFAULT,
                            server_.dispatcher());

  auto b = a.createChildManager(server_.dispatcher());
  auto d = b->createChildManager(server_.dispatcher());
  auto e = b->createChildManager(server_.dispatcher());

  auto c = a.createChildManager(server_.dispatcher());
  auto f = c->createChildManager(server_.dispatcher());
  auto g = c->createChildManager(server_.dispatcher());

  int call_count = 0;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_d;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_e;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_f;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_g;

  EXPECT_CALL(cb_d, Call(_)).WillOnce(Invoke([&call_count](std::chrono::milliseconds) {
    call_count++;
    return absl::OkStatus();
  }));
  EXPECT_CALL(cb_e, Call(_)).WillOnce(Invoke([&call_count](std::chrono::milliseconds) {
    call_count++;
    return absl::OkStatus();
  }));
  // validate neighbor remains uneffected
  EXPECT_CALL(cb_f, Call(_)).Times(0);
  EXPECT_CALL(cb_g, Call(_)).Times(0);

  auto handle_d = d->addOnDrainCloseCb(cb_d.AsStdFunction());
  auto handle_e = e->addOnDrainCloseCb(cb_e.AsStdFunction());
  auto handle_f = f->addOnDrainCloseCb(cb_f.AsStdFunction());
  auto handle_g = g->addOnDrainCloseCb(cb_g.AsStdFunction());

  b->startDrainSequence([] {});
  EXPECT_EQ(call_count, 2);
}

// Validate that draining of a child does not impact the drain-state of the parent
TEST_F(DrainManagerImplTest, DrainOnlyCascadesDownwards) {
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

  auto a = DrainManagerImpl(server_, envoy::config::listener::v3::Listener::DEFAULT,
                            server_.dispatcher());
  auto b = a.createChildManager(server_.dispatcher());
  auto c = b->createChildManager(server_.dispatcher());

  int call_count = 0;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_a;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_b;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb_c;

  // validate top-level callback is never fired
  EXPECT_CALL(cb_a, Call(_)).Times(0);
  EXPECT_CALL(cb_b, Call(_)).WillOnce(Invoke([&call_count](std::chrono::milliseconds) {
    call_count++;
    return absl::OkStatus();
  }));
  EXPECT_CALL(cb_c, Call(_)).WillOnce(Invoke([&call_count](std::chrono::milliseconds) {
    call_count++;
    return absl::OkStatus();
  }));
  auto handle_a = a.addOnDrainCloseCb(cb_a.AsStdFunction());
  auto handle_b = b->addOnDrainCloseCb(cb_b.AsStdFunction());
  auto handle_c = c->addOnDrainCloseCb(cb_c.AsStdFunction());

  // drain the middle of the tree
  b->startDrainSequence([] {});
  EXPECT_EQ(call_count, 2);
}

// Validate that we can initiate draining on a child (to no effect) after the parent
// has already started draining
TEST_F(DrainManagerImplTest, DrainChildExplicitlyAfterParent) {
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

  auto a = DrainManagerImpl(server_, envoy::config::listener::v3::Listener::DEFAULT,
                            server_.dispatcher());
  auto b = a.createChildManager(server_.dispatcher());
  auto c = b->createChildManager(server_.dispatcher());

  int call_count = 0;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb;

  // validate top-level callback is never fired
  EXPECT_CALL(cb, Call(_)).WillRepeatedly(Invoke([&call_count](std::chrono::milliseconds) {
    call_count++;
    return absl::OkStatus();
  }));
  auto handle_a = a.addOnDrainCloseCb(cb.AsStdFunction());
  auto handle_b = b->addOnDrainCloseCb(cb.AsStdFunction());
  auto handle_c = c->addOnDrainCloseCb(cb.AsStdFunction());

  // Drain the parent, then the child
  a.startDrainSequence([&] {});
  b->startDrainSequence([&] {});
  EXPECT_EQ(call_count, 3);
}

// Validate that we can initiate draining on a parent safely after a child has
// already started draining
TEST_F(DrainManagerImplTest, DrainParentAfterChild) {
  ON_CALL(server_.options_, drainStrategy()).WillByDefault(Return(Server::DrainStrategy::Gradual));
  ON_CALL(server_.options_, drainTime()).WillByDefault(Return(std::chrono::seconds(1)));

  auto a = DrainManagerImpl(server_, envoy::config::listener::v3::Listener::DEFAULT,
                            server_.dispatcher());
  auto b = a.createChildManager(server_.dispatcher());
  auto c = b->createChildManager(server_.dispatcher());

  int call_count = 0;
  testing::MockFunction<absl::Status(std::chrono::milliseconds)> cb;

  // validate top-level callback is never fired
  EXPECT_CALL(cb, Call(_)).WillRepeatedly(Invoke([&call_count](std::chrono::milliseconds) {
    call_count++;
    return absl::OkStatus();
  }));
  auto handle_a = a.addOnDrainCloseCb(cb.AsStdFunction());
  auto handle_b = b->addOnDrainCloseCb(cb.AsStdFunction());
  auto handle_c = c->addOnDrainCloseCb(cb.AsStdFunction());

  // Drain the child, then the parent
  b->startDrainSequence([] {});
  a.startDrainSequence([] {});
  EXPECT_EQ(call_count, 3);
}

} // namespace
} // namespace Server
} // namespace Envoy
