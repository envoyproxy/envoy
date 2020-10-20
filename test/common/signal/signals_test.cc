#include <sys/mman.h>

#include <csignal>

#include "envoy/server/fatal_action_config.h"

#include "common/signal/fatal_error_handler.h"
#include "common/signal/signal_action.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ASANITIZED /* Sanitized by Clang */
#endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#define ASANITIZED /* Sanitized by GCC */
#endif

// Use this test handler instead of a mock, because fatal error handlers must be
// signal-safe and a mock might allocate memory.
class TestFatalErrorHandler : public FatalErrorHandlerInterface {
  void onFatalError(std::ostream& os) const override { os << "HERE!"; }
  void runFatalActionsOnTrackedObject(
      std::list<const Server::Configuration::FatalActionPtr>& actions) const override {
    // Call the Fatal Actions with nullptr
    for (const auto& action : actions) {
      action->run(nullptr);
    }
  }
};

// Death tests that expect a particular output are disabled under address sanitizer.
// The sanitizer does its own special signal handling and prints messages that are
// not ours instead of what this test expects. As of latest Clang this appears
// to include abort() as well.
#ifndef ASANITIZED
TEST(SignalsDeathTest, InvalidAddressDeathTest) {
  SignalAction actions;
  EXPECT_DEATH(
      []() -> void {
        // Oops!
        volatile int* nasty_ptr = reinterpret_cast<int*>(0x0);
        *(nasty_ptr) = 0; // NOLINT(clang-analyzer-core.NullDereference)
      }(),
      "backtrace.*Segmentation fault");
}

TEST(SignalsDeathTest, RegisteredHandlerTest) {
  TestFatalErrorHandler handler;
  FatalErrorHandler::registerFatalErrorHandler(handler);
  SignalAction actions;
  // Make sure the fatal error log "HERE" registered above is logged on fatal error.
  EXPECT_DEATH(
      []() -> void {
        // Oops!
        volatile int* nasty_ptr = reinterpret_cast<int*>(0x0);
        *(nasty_ptr) = 0; // NOLINT(clang-analyzer-core.NullDereference)
      }(),
      "HERE");
  FatalErrorHandler::removeFatalErrorHandler(handler);
}

TEST(SignalsDeathTest, BusDeathTest) {
  SignalAction actions;
  EXPECT_DEATH(
      []() -> void {
        // Bus error is tricky. There's one way that can work on POSIX systems
        // described below but it depends on mmaping a file. Just make it easy and
        // raise a bus.
        //
        // FILE *f = tmpfile();
        // int *p = mmap(0, 4, PROT_WRITE, MAP_PRIVATE, fileno(f), 0);
        // *p = 0;
        raise(SIGBUS);
      }(),
      "backtrace.*Bus");
}

TEST(SignalsDeathTest, BadMathDeathTest) {
  SignalAction actions;
  EXPECT_DEATH(
      []() -> void {
        // It turns out to be really hard to not have the optimizer get rid of a
        // division by zero. Just raise the signal for this test.
        raise(SIGFPE);
      }(),
      "backtrace.*Floating point");
}

#if defined(__x86_64__) || defined(__i386__)
// Unfortunately we don't have a reliable way to do this on other platforms
TEST(SignalsDeathTest, IllegalInstructionDeathTest) {
  SignalAction actions;
  EXPECT_DEATH(
      []() -> void {
        // Intel defines the "ud2" opcode to be an invalid instruction:
        __asm__("ud2");
      }(),
      "backtrace.*Illegal");
}
#endif

TEST(SignalsDeathTest, AbortDeathTest) {
  SignalAction actions;
  EXPECT_DEATH([]() -> void { abort(); }(), "backtrace.*Abort(ed)?");
}

TEST(SignalsDeathTest, RestoredPreviousHandlerDeathTest) {
  SignalAction action;
  {
    SignalAction inner_action;
    // Test case for a previously encountered misfeature:
    // We should restore the previous SignalAction when the inner action
    // goes out of scope, NOT the default.
  }
  // Outer SignalAction should be active again:
  EXPECT_DEATH([]() -> void { abort(); }(), "backtrace.*Abort(ed)?");
}

#endif

TEST(SignalsDeathTest, IllegalStackAccessDeathTest) {
  SignalAction actions;
  EXPECT_DEATH(actions.tryEvilAccessForTest(false), "");
  EXPECT_DEATH(actions.tryEvilAccessForTest(true), "");
}

TEST(Signals, LegalTest) {
  // Don't do anything wrong.
  { SignalAction actions; }
  // Nothing should happen...
}

TEST(Signals, RaiseNonFatalTest) {
  {
    SignalAction actions;
    // I urgently request that you do nothing please!
    raise(SIGURG);
  }
  // Nothing should happen...
}

TEST(Signals, LegalStackAccessTest) {
  SignalAction actions;
  actions.doGoodAccessForTest();
}

TEST(Signals, HandlerTest) {
  siginfo_t fake_si;
  fake_si.si_addr = nullptr;
  SignalAction::sigHandler(SIGURG, &fake_si, nullptr);
}

TEST(FatalErrorHandler, CallHandler) {
  // Reserve space in advance so that the handler doesn't allocate memory.
  std::string s;
  s.reserve(1024);
  std::ostringstream os(std::move(s));

  TestFatalErrorHandler handler;
  FatalErrorHandler::registerFatalErrorHandler(handler);

  FatalErrorHandler::callFatalErrorHandlers(os);
  EXPECT_EQ(os.str(), "HERE!");

  // callFatalErrorHandlers() will unregister the handler, so this isn't
  // necessary for cleanup. Call it anyway, to simulate the case when one thread
  // tries to remove the handler while another thread crashes.
  FatalErrorHandler::removeFatalErrorHandler(handler);
}

// Use this specialized test handler instead of a mock, because fatal error
// handlers must be signal-safe and a mock might allocate memory.
class MemoryCheckingFatalErrorHandler : public FatalErrorHandlerInterface {
public:
  MemoryCheckingFatalErrorHandler(const Stats::TestUtil::MemoryTest& memory_test,
                                  uint64_t& allocated_after_call)
      : memory_test_(memory_test), allocated_after_call_(allocated_after_call) {}
  void onFatalError(std::ostream& os) const override {
    UNREFERENCED_PARAMETER(os);
    allocated_after_call_ = memory_test_.consumedBytes();
  }

  void runFatalActionsOnTrackedObject(
      std::list<const Server::Configuration::FatalActionPtr>& /*actions*/) const override {}

private:
  const Stats::TestUtil::MemoryTest& memory_test_;
  uint64_t& allocated_after_call_;
};

// FatalErrorHandler::callFatalErrorHandlers shouldn't allocate any heap memory,
// so that it's safe to call from a signal handler. Test by comparing the
// allocated memory before a call with the allocated memory during a handler.
TEST(FatalErrorHandler, DontAllocateMemory) {
  // Reserve space in advance so that the handler doesn't allocate memory.
  std::string s;
  s.reserve(1024);
  std::ostringstream os(std::move(s));

  Stats::TestUtil::MemoryTest memory_test;

  uint64_t allocated_after_call;
  MemoryCheckingFatalErrorHandler handler(memory_test, allocated_after_call);
  FatalErrorHandler::registerFatalErrorHandler(handler);

  uint64_t allocated_before_call = memory_test.consumedBytes();
  FatalErrorHandler::callFatalErrorHandlers(os);

  EXPECT_MEMORY_EQ(allocated_after_call, allocated_before_call);
}

TEST(FatalErrorHandler, ShouldOnlyBeAbleToRegisterFatalActionsOnce) {
  EXPECT_DEATH(
      {
        auto safe_actions =
            std::make_unique<std::list<const Server::Configuration::FatalActionPtr>>();
        auto unsafe_actions =
            std::make_unique<std::list<const Server::Configuration::FatalActionPtr>>();
        FatalErrorHandler::registerFatalActions(std::move(safe_actions), std::move(unsafe_actions),
                                                nullptr);
        auto additional_safe_actions =
            std::make_unique<std::list<const Server::Configuration::FatalActionPtr>>();
        auto additional_unsafe_actions =
            std::make_unique<std::list<const Server::Configuration::FatalActionPtr>>();
        // Subsequent call should trigger Envoy bug. We should only have this run
        // once.
        FatalErrorHandler::registerFatalActions(std::move(additional_safe_actions),
                                                std::move(additional_unsafe_actions), nullptr);
      },
      "Details: registerFatalActions called more than once.");
}

class TestFatalAction : public Server::Configuration::FatalAction {
public:
  TestFatalAction(bool is_safe) : is_safe_(is_safe) {}

  void run(const ScopeTrackedObject* /*current_object*/) override { ++times_ran; }

  bool isSafe() const override { return is_safe_; }

  int getNumTimesRan() { return times_ran; }

private:
  bool is_safe_;
  int times_ran = 0;
};

TEST(FatalErrorHandler, CanCallRegisteredActions) {
  // Set up Fatal Handlers
  TestFatalErrorHandler handler;
  FatalErrorHandler::registerFatalErrorHandler(handler);

  // Set up Fatal Actions
  Server::MockInstance instance;
  auto api_fake = Api::createApiForTest();
  EXPECT_CALL(instance, api()).WillRepeatedly(ReturnRef(*api_fake));

  auto safe_actions = std::make_unique<std::list<const Server::Configuration::FatalActionPtr>>();
  auto safe_fatal_action = std::make_unique<TestFatalAction>(true);
  auto* raw_safe_action = safe_fatal_action.get();
  safe_actions->emplace_back(std::move(safe_fatal_action));

  auto unsafe_actions = std::make_unique<std::list<const Server::Configuration::FatalActionPtr>>();
  auto unsafe_fatal_action = std::make_unique<TestFatalAction>(false);
  auto* raw_unsafe_action = unsafe_fatal_action.get();
  unsafe_actions->emplace_back(std::move(unsafe_fatal_action));

  FatalErrorHandler::registerFatalActions(std::move(safe_actions), std::move(unsafe_actions),
                                          &instance);
  // Call the actions
  EXPECT_TRUE(FatalErrorHandler::runSafeActions());
  EXPECT_TRUE(FatalErrorHandler::runUnsafeActions());

  // Expect ran once
  EXPECT_EQ(raw_safe_action->getNumTimesRan(), 1);
  EXPECT_EQ(raw_unsafe_action->getNumTimesRan(), 1);
}

} // namespace Envoy
