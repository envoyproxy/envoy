#include <atomic>
#include <csignal>
#include <memory>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/watchdog/backtrace_action/v3/backtrace_action.pb.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/thread/thread.h"

#include "source/common/signal/non_fatal_signal_action.h"
#include "source/common/signal/non_fatal_signal_handler.h"
#include "source/extensions/watchdog/backtrace_action/backtrace_action.h"
#include "source/server/backtrace.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace BacktraceAction {
namespace {

class BacktraceActionTest : public testing::Test {
protected:
  BacktraceActionTest()
      : api_(Api::createApiForTest(stats_)), dispatcher_(api_->allocateDispatcher("test")),
        context_({*api_, *dispatcher_, *stats_.rootScope(), "test"}) {}

  NonFatalSignalAction non_fatal_signal_action_;
  Stats::TestUtil::TestStore stats_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Server::Configuration::GuardDogActionFactoryContext context_;
  std::unique_ptr<Server::Configuration::GuardDogAction> action_;
};

class BacktraceActionNoSignalTest : public testing::Test {
protected:
  BacktraceActionNoSignalTest()
      : api_(Api::createApiForTest(stats_)), dispatcher_(api_->allocateDispatcher("test")),
        context_({*api_, *dispatcher_, *stats_.rootScope(), "test"}) {}

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

TEST_F(BacktraceActionNoSignalTest, WarnWhenSignalNotInstalled) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);

  const auto now = api_->timeSource().monotonicTime();
  const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {
      {Thread::ThreadId(10), now}};
  EXPECT_LOG_CONTAINS("warn", "signal handler not installed",
                      action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS,
                                   tid_ltt_pairs, now));
}

TEST_F(BacktraceActionTest, SingleBacktraceLogged) {
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
}

TEST_F(BacktraceActionTest, MultipleBacktracesLogged) {
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

TEST_F(BacktraceActionTest, InFlightSkipPreventsDuplicateBacktrace) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  config.mutable_cooldown_duration()->set_seconds(0);

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
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
  });

  EXPECT_TRUE(first_logged.WaitForNotificationWithTimeout(absl::Seconds(5)));

  // Allow any spurious timer to fire before asserting.
  absl::SleepFor(absl::Milliseconds(200));
  EXPECT_EQ(count.load(), 1);

  BackwardsTrace::setLogToStderr(prev_log_to_stderr);
  dispatcher_->exit();
  thread->join();
}

TEST_F(BacktraceActionTest, OnNonFatalSignalNullInfoIgnored) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, nullptr, nullptr);
}

TEST_F(BacktraceActionTest, OnNonFatalSignalWrongPidIgnored) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);
  siginfo_t info{};
  info.si_pid = 1; // PID 1 (init) is never our PID.
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, &info, nullptr);
}

TEST_F(BacktraceActionTest, OnNonFatalSignalNoMatchingSlot) {
  envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig config;
  action_ = std::make_unique<BacktraceAction>(config, context_);
  siginfo_t info{};
  info.si_pid = getpid();
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, &info, nullptr);
}

TEST_F(BacktraceActionNoSignalTest, WarnWhenSignalHandlerNotRegistered) {
  // Fill all handler slots with a dummy to force BacktraceAction registration to fail.
  auto dummy = +[](int, siginfo_t*, void*) {};
  for (size_t i = 0; i < NonFatalSignalHandler::MaxHandlers; ++i) {
    NonFatalSignalHandler::registerNonFatalSignalHandler(dummy);
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
    NonFatalSignalHandler::removeNonFatalSignalHandler(dummy);
  }
}

} // namespace
} // namespace BacktraceAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
