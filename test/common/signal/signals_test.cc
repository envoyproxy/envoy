#include <sys/mman.h>

#include <csignal>
#include <vector>

#include "envoy/common/scope_tracker.h"

#include "source/common/signal/fatal_error_handler.h"
#include "source/common/signal/signal_action.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ASANITIZED /* Sanitized by Clang */
#endif

#if __has_feature(memory_sanitizer)
#define MSANITIZED /* Sanitized by Clang */
#endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#define ASANITIZED /* Sanitized by GCC */
#endif

namespace FatalErrorHandler {

extern void resetFatalActionStateForTest();

} // namespace FatalErrorHandler

// Use this test handler instead of a mock, because fatal error handlers must be
// signal-safe and a mock might allocate memory.
class TestFatalErrorHandler : public FatalErrorHandlerInterface {
public:
  void onFatalError(std::ostream& os) const override { os << "HERE!"; }
  void
  runFatalActionsOnTrackedObject(const FatalAction::FatalActionPtrList& actions) const override {
    // Run the actions
    for (const auto& action : actions) {
      action->run(tracked_objects_);
    }
  }

private:
  std::vector<const ScopeTrackedObject*> tracked_objects_{nullptr};
};

// Use this to test fatal actions get called, as well as the order they run.
class EchoFatalAction : public Server::Configuration::FatalAction {
public:
  EchoFatalAction(absl::string_view echo_msg) : echo_msg_(echo_msg) {}
  void run(absl::Span<const ScopeTrackedObject* const> /*tracked_objects*/) override {
    std::cerr << echo_msg_;
  }
  bool isAsyncSignalSafe() const override { return true; }

private:
  const std::string echo_msg_;
};

// Use this to test failing while in a signal handler.
class SegfaultFatalAction : public Server::Configuration::FatalAction {
public:
  void run(absl::Span<const ScopeTrackedObject* const> /*tracked_objects*/) override {
    raise(SIGSEGV);
  }
  bool isAsyncSignalSafe() const override { return false; }
};

// Death tests that expect a particular output are disabled under address sanitizer.
// The sanitizer does its own special signal handling and prints messages that are
// not ours instead of what this test expects. As of latest Clang this appears
// to include abort() as well.
#if !defined(ASANITIZED) && !defined(MSANITIZED)
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

TEST(SignalsDeathTest, CanRunAllFatalActions) {
  SignalAction actions;
  TestFatalErrorHandler handler;
  FatalErrorHandler::registerFatalErrorHandler(handler);

  FatalAction::FatalActionPtrList safe_actions;
  FatalAction::FatalActionPtrList unsafe_actions;

  safe_actions.emplace_back(std::make_unique<EchoFatalAction>("Safe Action!"));
  unsafe_actions.emplace_back(std::make_unique<EchoFatalAction>("Unsafe Action!"));
  FatalErrorHandler::registerFatalActions(std::move(safe_actions), std::move(unsafe_actions),
                                          Thread::threadFactoryForTest());
  EXPECT_DEATH([]() -> void { raise(SIGSEGV); }(), "Safe Action!.*HERE.*Unsafe Action!");
  FatalErrorHandler::removeFatalErrorHandler(handler);
  FatalErrorHandler::resetFatalActionStateForTest();
}

TEST(SignalsDeathTest, ShouldJustExitIfFatalActionsRaiseAnotherSignal) {
  SignalAction actions;
  TestFatalErrorHandler handler;
  FatalErrorHandler::registerFatalErrorHandler(handler);

  FatalAction::FatalActionPtrList safe_actions;
  FatalAction::FatalActionPtrList unsafe_actions;

  unsafe_actions.emplace_back(std::make_unique<SegfaultFatalAction>());
  FatalErrorHandler::registerFatalActions(std::move(safe_actions), std::move(unsafe_actions),
                                          Thread::threadFactoryForTest());

  EXPECT_DEATH([]() -> void { raise(SIGABRT); }(), "Our FatalActions triggered a fatal signal.");
  FatalErrorHandler::removeFatalErrorHandler(handler);
  FatalErrorHandler::resetFatalActionStateForTest();
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
  MemoryCheckingFatalErrorHandler(const Memory::TestUtil::MemoryTest& memory_test,
                                  uint64_t& allocated_after_call)
      : memory_test_(memory_test), allocated_after_call_(allocated_after_call) {}
  void onFatalError(std::ostream& os) const override {
    UNREFERENCED_PARAMETER(os);
    allocated_after_call_ = memory_test_.consumedBytes();
  }

  void runFatalActionsOnTrackedObject(const FatalAction::FatalActionPtrList&
                                      /*actions*/) const override {}

private:
  const Memory::TestUtil::MemoryTest& memory_test_;
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

  Memory::TestUtil::MemoryTest memory_test;

  uint64_t allocated_after_call;
  MemoryCheckingFatalErrorHandler handler(memory_test, allocated_after_call);
  FatalErrorHandler::registerFatalErrorHandler(handler);

  uint64_t allocated_before_call = memory_test.consumedBytes();
  FatalErrorHandler::callFatalErrorHandlers(os);

  EXPECT_MEMORY_EQ(allocated_after_call, allocated_before_call);
}

} // namespace Envoy
