#include "envoy/server/fatal_action_config.h"

#include "common/signal/fatal_error_handler.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace FatalErrorHandler {

extern void resetFatalActionState();
} // namespace FatalErrorHandler
namespace FatalAction {

// Use this test handler instead of a mock, because fatal error handlers must be
// signal-safe and a mock might allocate memory.
class TestFatalErrorHandler : public FatalErrorHandlerInterface {
  void onFatalError(std::ostream& /*os*/) const override {}
  void
  runFatalActionsOnTrackedObject(const FatalAction::FatalActionPtrList& actions) const override {
    // Call the Fatal Actions with nullptr
    for (const Server::Configuration::FatalActionPtr& action : actions) {
      action->run(nullptr);
    }
  }
};

class TestFatalAction : public Server::Configuration::FatalAction {
public:
  TestFatalAction(bool is_safe, int* const counter) : is_safe_(is_safe), counter_(counter) {}
  void run(const ScopeTrackedObject* /*current_object*/) override { ++(*counter_); }
  bool isAsyncSignalSafe() const override { return is_safe_; }

private:
  bool is_safe_;
  int* counter_;
};

class FatalActionTest : public ::testing::Test {
public:
  FatalActionTest() : handler_(std::make_unique<TestFatalErrorHandler>()) {
    FatalErrorHandler::registerFatalErrorHandler(*handler_);
  }

protected:
  void TearDown() override {
    // Reset module state
    FatalErrorHandler::resetFatalActionState();
    FatalErrorHandler::removeFatalErrorHandler(*handler_);
  }

  std::unique_ptr<TestFatalErrorHandler> handler_;
  FatalAction::FatalActionPtrList safe_actions_;
  FatalAction::FatalActionPtrList unsafe_actions_;
  int counter_ = 0;
};

TEST_F(FatalActionTest, ShouldNotBeAbleToRunActionsBeforeRegistration) {
  // Call the actions
  EXPECT_FALSE(FatalErrorHandler::runSafeActions());
  EXPECT_FALSE(FatalErrorHandler::runUnsafeActions());
}

TEST_F(FatalActionTest, ShouldOnlyBeAbleToRegisterFatalActionsOnce) {
  // Register empty list of actions
  FatalErrorHandler::registerFatalActions(FatalAction::FatalActionPtrList(),
                                          FatalAction::FatalActionPtrList(),
                                          Thread::threadFactoryForTest());

  safe_actions_.emplace_back(std::make_unique<TestFatalAction>(true, &counter_));
  FatalErrorHandler::registerFatalActions(std::move(safe_actions_), std::move(unsafe_actions_),
                                          Thread::threadFactoryForTest());

  EXPECT_TRUE(FatalErrorHandler::runSafeActions());
  EXPECT_EQ(counter_, 0);
}

TEST_F(FatalActionTest, CanCallRegisteredActions) {
  // Set up Fatal Actions
  safe_actions_.emplace_back(std::make_unique<TestFatalAction>(true, &counter_));
  unsafe_actions_.emplace_back(std::make_unique<TestFatalAction>(false, &counter_));
  FatalErrorHandler::registerFatalActions(std::move(safe_actions_), std::move(unsafe_actions_),
                                          Thread::threadFactoryForTest());

  // Call the actions and check they increment the counter.
  EXPECT_TRUE(FatalErrorHandler::runSafeActions());
  EXPECT_EQ(counter_, 1);

  EXPECT_TRUE(FatalErrorHandler::runUnsafeActions());
  EXPECT_EQ(counter_, 2);
}

TEST_F(FatalActionTest, CanOnlyRunSafeActionsOnce) {
  FatalErrorHandler::registerFatalActions(std::move(safe_actions_), std::move(unsafe_actions_),
                                          Thread::threadFactoryForTest());
  ASSERT_TRUE(FatalErrorHandler::runSafeActions());

  // This should return false since they've ran already.
  EXPECT_FALSE(FatalErrorHandler::runSafeActions());
}

TEST_F(FatalActionTest, ShouldOnlyBeAbleToRunUnsafeActionsFromThreadThatRanSafeActions) {
  FatalErrorHandler::registerFatalActions(std::move(safe_actions_), std::move(unsafe_actions_),
                                          Thread::threadFactoryForTest());

  // Jump to run unsafe actions, without running safe actions.
  ASSERT_FALSE(FatalErrorHandler::runUnsafeActions());

  absl::Notification run_unsafe_actions;
  absl::Notification ran_safe_actions;
  auto fatal_action_thread =
      Thread::threadFactoryForTest().createThread([&run_unsafe_actions, &ran_safe_actions]() {
        // Run Safe Actions and notify
        EXPECT_TRUE(FatalErrorHandler::runSafeActions());
        ran_safe_actions.Notify();

        run_unsafe_actions.WaitForNotification();
        EXPECT_TRUE(FatalErrorHandler::runUnsafeActions());
      });

  // Wait for other thread to run safe actions, then try to run safe and
  // unsafe actions, they should both not run for this thread.
  ran_safe_actions.WaitForNotification();
  ASSERT_FALSE(FatalErrorHandler::runSafeActions());
  ASSERT_FALSE(FatalErrorHandler::runUnsafeActions());
  run_unsafe_actions.Notify();

  fatal_action_thread->join();
}

} // namespace FatalAction
} // namespace Envoy
