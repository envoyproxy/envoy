#include <atomic>
#include <csignal>
#include <limits>
#include <memory>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/watchdog/backtrace_action/v3/backtrace_action.pb.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/thread/thread.h"

#include "source/common/signal/non_fatal_signal_handler.h"
#include "source/extensions/watchdog/backtrace_action/backtrace_action.h"
#include "source/server/backtrace.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace BacktraceAction {

class BacktraceActionPeer {
public:
  static void reset() {
    for (auto& slot : BacktraceAction::signal_slots_) {
      slot.state.store(BacktraceAction::SlotState::Free);
      slot.tid.store(0);
    }
  }
  static void signalSlot(int i, int64_t tid) {
    BacktraceAction::signal_slots_[i].tid.store(tid);
    BacktraceAction::signal_slots_[i].state.store(BacktraceAction::SlotState::Signaled);
  }
  static void writingSlot(int i) {
    BacktraceAction::signal_slots_[i].state.store(BacktraceAction::SlotState::Writing);
  }
  static void claimSlot(int i) {
    BacktraceAction::signal_slots_[i].state.store(BacktraceAction::SlotState::Claimed);
  }
  static bool isClaimed(int i) {
    return BacktraceAction::signal_slots_[i].state.load() == BacktraceAction::SlotState::Claimed;
  }
  static bool isWriting(int i) {
    return BacktraceAction::signal_slots_[i].state.load() == BacktraceAction::SlotState::Writing;
  }
  static bool isFree(int i) {
    return BacktraceAction::signal_slots_[i].state.load() == BacktraceAction::SlotState::Free;
  }
  static bool isSignaled(int i) {
    return BacktraceAction::signal_slots_[i].state.load() == BacktraceAction::SlotState::Signaled;
  }
  static bool isReady(int i) {
    return BacktraceAction::signal_slots_[i].state.load() == BacktraceAction::SlotState::Ready;
  }
  static int64_t slotTid(int i) { return BacktraceAction::signal_slots_[i].tid.load(); }
  static void fireTimer(Server::Configuration::GuardDogAction& action, int i) {
    static_cast<BacktraceAction&>(action).onSlotTimer(i);
  }
  static constexpr int maxSlots() { return BacktraceAction::MaxSlots; }
};

namespace {

class BacktraceActionTest : public testing::Test {
protected:
  BacktraceActionTest()
      : api_(Api::createApiForTest(stats_)), dispatcher_(api_->allocateDispatcher("test")),
        context_({*api_, *dispatcher_, *stats_.rootScope(), "test"}) {}

  void SetUp() override { BacktraceActionPeer::reset(); }

  Stats::TestUtil::TestStore stats_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Server::Configuration::GuardDogActionFactoryContext context_;
  std::unique_ptr<Server::Configuration::GuardDogAction> action_;
};

TEST_F(BacktraceActionTest, WarnOnEmptyThreadList) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  const auto now = api_->timeSource().monotonicTime();
  EXPECT_LOG_CONTAINS(
      "warn", "no tids were provided",
      action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, {}, now));
}

TEST_F(BacktraceActionTest, SingleBacktraceLogged) {
#ifndef __linux__
  GTEST_SKIP() << "signalThread (per-thread signaling) is not supported on this platform.";
#endif
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  config.mutable_cooldown_duration()->set_seconds(20);

  const bool prev_log_to_stderr = BackwardsTrace::logToStderr();
  BackwardsTrace::setLogToStderr(false);

  action_ = std::make_unique<BacktraceAction>(config, context_);

  Thread::ThreadId child_tid;
  absl::Notification child_ready;
  Thread::ThreadPtr thread =
      api_->threadFactory().createThread([this, &child_tid, &child_ready]() -> void {
        child_tid = api_->threadFactory().currentThreadId();
        child_ready.Notify();
        dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
      });
  child_ready.WaitForNotification();

  const auto now = api_->timeSource().monotonicTime();
  const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {{child_tid, now}};

  absl::Notification logged;
  LogLevelSetter save_levels(spdlog::level::trace);
  LogExpectation expectation(GetLogSink(), [&](Logger::Logger::Levels, const std::string& msg) {
    if (msg.find("Envoy version:") != std::string::npos) {
      logged.Notify();
    }
  });

  dispatcher_->post([&]() {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
  });

  EXPECT_TRUE(logged.WaitForNotificationWithTimeout(absl::Seconds(5)));

  BackwardsTrace::setLogToStderr(prev_log_to_stderr);
  dispatcher_->exit();
  thread->join();

  EXPECT_EQ(1U, stats_.counter("watchdog.backtrace_action.backtraces_logged").value());
  EXPECT_EQ(0U, stats_.counter("watchdog.backtrace_action.backtraces_failed").value());
}

TEST_F(BacktraceActionTest, MultipleBacktracesLogged) {
#ifndef __linux__
  GTEST_SKIP() << "signalThread (per-thread signaling) is not supported on this platform.";
#endif
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  config.mutable_cooldown_duration()->set_seconds(20);

  const bool prev_log_to_stderr = BackwardsTrace::logToStderr();
  BackwardsTrace::setLogToStderr(false);

  action_ = std::make_unique<BacktraceAction>(config, context_);

  Thread::ThreadId tid1, tid2;
  absl::Notification ready1, ready2, done2;

  Thread::ThreadPtr thread1 = api_->threadFactory().createThread([this, &tid1, &ready1]() -> void {
    tid1 = api_->threadFactory().currentThreadId();
    ready1.Notify();
    dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  });
  Thread::ThreadPtr thread2 =
      api_->threadFactory().createThread([this, &tid2, &ready2, &done2]() -> void {
        tid2 = api_->threadFactory().currentThreadId();
        ready2.Notify();
        done2.WaitForNotification();
      });

  ready1.WaitForNotification();
  ready2.WaitForNotification();

  const auto now = api_->timeSource().monotonicTime();
  const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {{tid1, now},
                                                                                 {tid2, now}};

  std::atomic<int> count{0};
  absl::Notification all_logged;
  LogLevelSetter save_levels(spdlog::level::trace);
  LogExpectation expectation(GetLogSink(), [&](Logger::Logger::Levels, const std::string& msg) {
    if (msg.find("Envoy version:") != std::string::npos) {
      if (++count == 2) {
        all_logged.Notify();
      }
    }
  });

  dispatcher_->post([&]() {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
  });

  EXPECT_TRUE(all_logged.WaitForNotificationWithTimeout(absl::Seconds(5)));

  BackwardsTrace::setLogToStderr(prev_log_to_stderr);
  done2.Notify();
  dispatcher_->exit();
  thread1->join();
  thread2->join();
}

TEST_F(BacktraceActionTest, CooldownPreventsDuplicateBacktrace) {
#ifndef __linux__
  GTEST_SKIP() << "signalThread (per-thread signaling) is not supported on this platform.";
#endif
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  config.mutable_cooldown_duration()->set_seconds(60);

  const bool prev_log_to_stderr = BackwardsTrace::logToStderr();
  BackwardsTrace::setLogToStderr(false);

  action_ = std::make_unique<BacktraceAction>(config, context_);

  Thread::ThreadId child_tid;
  absl::Notification child_ready;
  Thread::ThreadPtr thread =
      api_->threadFactory().createThread([this, &child_tid, &child_ready]() -> void {
        child_tid = api_->threadFactory().currentThreadId();
        child_ready.Notify();
        dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
      });
  child_ready.WaitForNotification();

  const auto now = api_->timeSource().monotonicTime();
  const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {{child_tid, now}};

  std::atomic<int> count{0};
  absl::Notification first_logged;
  LogLevelSetter save_levels(spdlog::level::trace);
  LogExpectation expectation(GetLogSink(), [&](Logger::Logger::Levels, const std::string& msg) {
    if (msg.find("Envoy version:") != std::string::npos) {
      if (count.fetch_add(1, std::memory_order_relaxed) == 0) {
        first_logged.Notify();
      }
    }
  });

  dispatcher_->post([&]() {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
  });
  EXPECT_TRUE(first_logged.WaitForNotificationWithTimeout(absl::Seconds(5)));

  // Second run with the same TID and same timestamp: cooldown should suppress it.
  absl::Notification second_run_done;
  dispatcher_->post([&]() {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
    second_run_done.Notify();
  });
  EXPECT_TRUE(second_run_done.WaitForNotificationWithTimeout(absl::Seconds(5)));

  // Allow any spurious timer to fire before asserting.
  absl::SleepFor(absl::Milliseconds(200));
  EXPECT_EQ(count.load(), 1);

  BackwardsTrace::setLogToStderr(prev_log_to_stderr);
  dispatcher_->exit();
  thread->join();
}

TEST_F(BacktraceActionTest, FreesSlotWhenThreadDoesNotRespond) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  // A signaled slot whose handler never ran (the target thread never responded).
  BacktraceActionPeer::signalSlot(0, api_->threadFactory().currentThreadId().getId());
  BacktraceActionPeer::fireTimer(*action_, 0);

  // This is assumed to be a failure. The slot should be freed for future use.
  EXPECT_TRUE(BacktraceActionPeer::isFree(0));
  EXPECT_EQ(1U, stats_.counter("watchdog.backtrace_action.backtraces_failed").value());
  EXPECT_EQ(0U, stats_.counter("watchdog.backtrace_action.backtraces_logged").value());
}

TEST(BacktraceActionWritingTest, TimerReArmsWhileSlotIsBeingWritten) {
  Stats::TestUtil::TestStore stats;
  Event::MockDispatcher dispatcher;
  std::vector<testing::NiceMock<Event::MockTimer>*> timers;
  EXPECT_CALL(dispatcher, createTimer_(testing::_))
      .WillRepeatedly(testing::Invoke([&](Event::TimerCb) -> Event::Timer* {
        auto* timer = new testing::NiceMock<Event::MockTimer>();
        timers.push_back(timer);
        return timer;
      }));
  Api::ApiPtr api = Api::createApiForTest(stats);
  Server::Configuration::GuardDogActionFactoryContext context{*api, dispatcher, *stats.rootScope(),
                                                              "test"};
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  BacktraceAction action(config, context);

  BacktraceActionPeer::writingSlot(0);

  // If the timer fires while the slot is having the trace written to
  // it, it should re-arm the timer to wait until it is ready.
  EXPECT_CALL(*timers[0], enableTimer(testing::_, testing::_));
  BacktraceActionPeer::fireTimer(action, 0);

  EXPECT_TRUE(BacktraceActionPeer::isWriting(0));
  EXPECT_EQ(0U, stats.counter("watchdog.backtrace_action.backtraces_failed").value());
  EXPECT_EQ(0U, stats.counter("watchdog.backtrace_action.backtraces_logged").value());
}

TEST_F(BacktraceActionTest, OnNonFatalSignalNullInfoIgnored) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  BacktraceActionPeer::signalSlot(0, api_->threadFactory().currentThreadId().getId());
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, nullptr, nullptr);

  // Slot should remain in signaled state since the signal handler no-ops on null info.
  EXPECT_TRUE(BacktraceActionPeer::isSignaled(0));
}

TEST_F(BacktraceActionTest, OnNonFatalSignalWrongPidIgnored) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  BacktraceActionPeer::signalSlot(0, api_->threadFactory().currentThreadId().getId());
  siginfo_t info{};
  info.si_pid = 1; // PID 1 (init) is never our PID.
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, &info, nullptr);

  // Slot should remain in signaled state since the signal was from a different PID.
  EXPECT_TRUE(BacktraceActionPeer::isSignaled(0));
}

TEST_F(BacktraceActionTest, OnNonFatalSignalNoSignaledSlot) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  siginfo_t info{};
  info.si_pid = getpid();
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, &info, nullptr);

  // Slots should only be written to if they are in a signaled state.
  for (int i = 0; i < BacktraceActionPeer::maxSlots(); ++i) {
    EXPECT_TRUE(BacktraceActionPeer::isFree(i));
  }
}

TEST_F(BacktraceActionTest, TimerNoOpOnFreeSlot) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  // A timer firing on an already-freed slot should be a no-op.
  ASSERT_TRUE(BacktraceActionPeer::isFree(0));
  BacktraceActionPeer::fireTimer(*action_, 0);

  EXPECT_TRUE(BacktraceActionPeer::isFree(0));
  EXPECT_EQ(0U, stats_.counter("watchdog.backtrace_action.backtraces_failed").value());
  EXPECT_EQ(0U, stats_.counter("watchdog.backtrace_action.backtraces_logged").value());
}

TEST_F(BacktraceActionTest, TimerNoOpOnClaimedSlot) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  // A slot still being claimed by run() must be left untouched by the timer.
  BacktraceActionPeer::claimSlot(0);
  BacktraceActionPeer::fireTimer(*action_, 0);

  EXPECT_TRUE(BacktraceActionPeer::isClaimed(0));
  EXPECT_EQ(0U, stats_.counter("watchdog.backtrace_action.backtraces_failed").value());
  EXPECT_EQ(0U, stats_.counter("watchdog.backtrace_action.backtraces_logged").value());
}

TEST_F(BacktraceActionTest, OnNonFatalSignalWritesTraceWithoutContext) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  BacktraceActionPeer::signalSlot(0, api_->threadFactory().currentThreadId().getId());
  siginfo_t info{};
  info.si_pid = getpid();

  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, &info, nullptr);

  // The handler should have written the trace and published it for the timer.
  EXPECT_TRUE(BacktraceActionPeer::isReady(0));
}

TEST_F(BacktraceActionTest, RunFailsToSignalNonexistentThread) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  // A TID that does not correspond to any live thread in this process, so signal delivery fails.
  const auto now = api_->timeSource().monotonicTime();
  const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> pairs = {
      {Thread::ThreadId(std::numeric_limits<int32_t>::max()), now}};

  EXPECT_LOG_CONTAINS(
      "warn", "failed to signal thread",
      action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, pairs, now));

  // The claimed slot should have been released and the failure counted.
  EXPECT_TRUE(BacktraceActionPeer::isFree(0));
  EXPECT_EQ(1U, stats_.counter("watchdog.backtrace_action.backtraces_failed").value());
  EXPECT_EQ(0U, stats_.counter("watchdog.backtrace_action.backtraces_logged").value());
}

TEST_F(BacktraceActionTest, WarnWhenSignalHandlerNotRegistered) {
  // Fill all handler slots with a no-op handler to force BacktraceAction registration to fail.
  auto noopHandler = +[](int, siginfo_t*, void*) {};
  for (size_t i = 0; i < NonFatalSignalHandler::MaxHandlers; ++i) {
    NonFatalSignalHandler::registerNonFatalSignalHandler(noopHandler);
  }

  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  const auto now = api_->timeSource().monotonicTime();
  const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> pairs = {
      {Thread::ThreadId(1), now}};
  EXPECT_LOG_CONTAINS(
      "warn", "signal handler not registered",
      action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, pairs, now));

  for (size_t i = 0; i < NonFatalSignalHandler::MaxHandlers; ++i) {
    NonFatalSignalHandler::removeNonFatalSignalHandler(noopHandler);
  }
}

} // namespace
} // namespace BacktraceAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
