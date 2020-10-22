#include "envoy/server/fatal_action_config.h"

#include "common/signal/fatal_error_handler.h"
#include "common/signal/signal_action.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
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

TEST(FatalActionTest, ShouldOnlyBeAbleToRegisterFatalActionsOnce) {
  EXPECT_DEATH(
      {
        FatalAction::FatalActionPtrList safe_actions;
        FatalAction::FatalActionPtrList unsafe_actions;
        FatalErrorHandler::registerFatalActions(safe_actions, unsafe_actions, nullptr);

        // Subsequent call should trigger Envoy bug. We should only have this run
        // once.
        FatalErrorHandler::registerFatalActions(safe_actions, unsafe_actions, nullptr);
      },
      "Details: registerFatalActions called more than once.");
}

class TestFatalAction : public Server::Configuration::FatalAction {
public:
  TestFatalAction(bool is_safe) : is_safe_(is_safe) {}

  void run(const ScopeTrackedObject* /*current_object*/) override { ++times_ran; }

  bool isAsyncSignalSafe() const override { return is_safe_; }

  int getNumTimesRan() { return times_ran; }

private:
  bool is_safe_;
  int times_ran = 0;
};

TEST(FatalActionTest, CanCallRegisteredActions) {
  // Set up Fatal Handlers
  TestFatalErrorHandler handler;
  FatalErrorHandler::registerFatalErrorHandler(handler);

  // Set up Fatal Actions
  Server::MockInstance instance;
  FatalAction::FatalActionPtrList safe_actions;
  FatalAction::FatalActionPtrList unsafe_actions;
  auto api_fake = Api::createApiForTest();
  EXPECT_CALL(instance, api()).WillRepeatedly(ReturnRef(*api_fake));

  safe_actions.emplace_back(std::make_unique<TestFatalAction>(true));
  auto* raw_safe_action = dynamic_cast<TestFatalAction*>(safe_actions.front().get());

  unsafe_actions.emplace_back(std::make_unique<TestFatalAction>(false));
  auto* raw_unsafe_action = dynamic_cast<TestFatalAction*>(unsafe_actions.front().get());

  FatalErrorHandler::registerFatalActions(safe_actions, unsafe_actions, &instance);

  // Call the actions
  EXPECT_TRUE(FatalErrorHandler::runSafeActions());
  EXPECT_TRUE(FatalErrorHandler::runUnsafeActions());

  // Expect ran once
  EXPECT_EQ(raw_safe_action->getNumTimesRan(), 1);
  EXPECT_EQ(raw_unsafe_action->getNumTimesRan(), 1);
}

} // namespace FatalAction
} // namespace Envoy
