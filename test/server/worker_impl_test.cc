#include "common/event/dispatcher_impl.h"

#include "server/worker_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Server {

class WorkerImplTest : public testing::Test {
public:
  WorkerImplTest() {
    // In the real worker the watchdog has timers that prevent exit. Here we need to prevent event
    // loop exit since we use mock timers.
    no_exit_timer_->enableTimer(std::chrono::hours(1));
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::DispatcherImpl* dispatcher_ = new Event::DispatcherImpl();
  Network::MockConnectionHandler* handler_ = new Network::MockConnectionHandler();
  NiceMock<MockGuardDog> guard_dog_;
  DefaultTestHooks hooks_;
  WorkerImpl worker_{tls_, hooks_, Event::DispatcherPtr{dispatcher_},
                     Network::ConnectionHandlerPtr{handler_}};
  Event::TimerPtr no_exit_timer_ = dispatcher_->createTimer([]() -> void {});
};

TEST_F(WorkerImplTest, BasicFlow) {
  InSequence s;
  std::thread::id current_thread_id = std::this_thread::get_id();
  ConditionalInitializer ci;

  // Before a worker is started adding a listener will be posted and will get added when the
  // thread starts running.
  NiceMock<Network::MockListenerConfig> listener;
  ON_CALL(listener, listenerTag()).WillByDefault(Return(1UL));
  EXPECT_CALL(*handler_, addListener(_))
      .WillOnce(Invoke([current_thread_id](Network::ListenerConfig& config) -> void {
        EXPECT_EQ(config.listenerTag(), 1UL);
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      }));
  worker_.addListener(listener, [&ci](bool success) -> void {
    EXPECT_TRUE(success);
    ci.setReady();
  });

  worker_.start(guard_dog_);
  ci.waitReady();

  // After a worker is started adding/stopping/removing a listener happens on the worker thread.
  NiceMock<Network::MockListenerConfig> listener2;
  ON_CALL(listener2, listenerTag()).WillByDefault(Return(2UL));
  EXPECT_CALL(*handler_, addListener(_))
      .WillOnce(Invoke([current_thread_id](Network::ListenerConfig& config) -> void {
        EXPECT_EQ(config.listenerTag(), 2UL);
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      }));
  worker_.addListener(listener2, [&ci](bool success) -> void {
    EXPECT_TRUE(success);
    ci.setReady();
  });
  ci.waitReady();

  EXPECT_CALL(*handler_, stopListeners(2))
      .WillOnce(InvokeWithoutArgs([current_thread_id, &ci]() -> void {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
        ci.setReady();
      }));
  worker_.stopListener(listener2);
  ci.waitReady();

  EXPECT_CALL(*handler_, removeListeners(2))
      .WillOnce(InvokeWithoutArgs([current_thread_id]() -> void {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      }));
  worker_.removeListener(listener2, [current_thread_id, &ci]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
    ci.setReady();
  });
  ci.waitReady();

  // Now test adding and removing a listener without stopping it first.
  NiceMock<Network::MockListenerConfig> listener3;
  ON_CALL(listener3, listenerTag()).WillByDefault(Return(3UL));
  EXPECT_CALL(*handler_, addListener(_))
      .WillOnce(Invoke([current_thread_id](Network::ListenerConfig& config) -> void {
        EXPECT_EQ(config.listenerTag(), 3UL);
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      }));
  worker_.addListener(listener3, [&ci](bool success) -> void {
    EXPECT_TRUE(success);
    ci.setReady();
  });
  ci.waitReady();

  EXPECT_CALL(*handler_, removeListeners(3))
      .WillOnce(InvokeWithoutArgs([current_thread_id]() -> void {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      }));
  worker_.removeListener(listener3, [current_thread_id]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
  });

  worker_.stop();
}

TEST_F(WorkerImplTest, ListenerException) {
  InSequence s;

  NiceMock<Network::MockListenerConfig> listener;
  ON_CALL(listener, listenerTag()).WillByDefault(Return(1UL));
  EXPECT_CALL(*handler_, addListener(_))
      .WillOnce(Throw(Network::CreateListenerException("failed")));
  worker_.addListener(listener, [](bool success) -> void { EXPECT_FALSE(success); });

  worker_.start(guard_dog_);
  worker_.stop();
}

} // namespace Server
} // namespace Envoy
