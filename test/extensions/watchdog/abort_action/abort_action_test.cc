#include <csignal>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/watchdog/abort_action/v3alpha/abort_action.pb.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/thread/thread.h"

#include "extensions/watchdog/abort_action/abort_action.h"
#include "extensions/watchdog/abort_action/config.h"

#include "test/test_common/utility.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace AbortAction {
namespace {

using AbortActionConfig = envoy::extensions::watchdog::abort_action::v3alpha::AbortActionConfig;

class AbortActionTest : public testing::Test {
protected:
  AbortActionTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        context_({*api_, *dispatcher_}) {}

  void waitForOutstandingNotify() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    mutex_.Await(absl::Condition(
        +[](int* outstanding_notifies) -> bool { return *outstanding_notifies > 0; },
        &outstanding_notifies_));
    outstanding_notifies_ -= 1;
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Server::Configuration::GuardDogActionFactoryContext context_;
  std::unique_ptr<Server::Configuration::GuardDogAction> action_;

  // Used to synchronize with the dispatch thread
  absl::Mutex mutex_;
  int outstanding_notifies_ ABSL_GUARDED_BY(mutex_) = 0;
};

TEST_F(AbortActionTest, ShouldNotAbortIfNoTids) {
  AbortActionConfig config;
  config.mutable_wait_duration()->set_nanos(1000000);
  action_ = std::make_unique<AbortAction>(config, context_);

  // Create empty vector and run the action.
  const auto now = api_->timeSource().monotonicTime();
  const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {};

  // Should not signal or panic since there are no TIDs.
  action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::KILL, tid_ltt_pairs, now);
}

// insufficient signal support on Windows.
#ifndef WIN32
TEST_F(AbortActionTest, CanKillThread) {
  AbortActionConfig config;
  config.mutable_wait_duration()->set_seconds(1);
  action_ = std::make_unique<AbortAction>(config, context_);

  auto die_function = [this]() -> void {
    // Create a thread that we'll kill
    Thread::ThreadId tid;
    Thread::ThreadPtr thread = api_->threadFactory().createThread([this, &tid]() -> void {
      tid = api_->threadFactory().currentThreadId();

      // Signal to test thread that tid has been set.
      {
        absl::MutexLock lock(&mutex_);
        outstanding_notifies_ += 1;
      }

      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    });

    // Wait for child thread to notify of its tid
    {
      absl::MutexLock lock(&mutex_);
      waitForOutstandingNotify();
    }

    // Create vector with child tid and run the action.
    const auto now = api_->timeSource().monotonicTime();
    const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {{tid, now}};

    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::KILL, tid_ltt_pairs, now);
  };

  EXPECT_DEATH(die_function(), "Caught Aborted");
}

void handler(int sig, siginfo_t* /*siginfo*/, void* /*context*/) {
  std::cout << "Eating signal :" << std::to_string(sig) << ". will ignore it." << std::endl;
  signal(SIGABRT, SIG_IGN);
}

TEST_F(AbortActionTest, PanicsIfThreadDoesNotDie) {
  AbortActionConfig config;
  config.mutable_wait_duration()->set_seconds(1);
  action_ = std::make_unique<AbortAction>(config, context_);

  auto die_function = [this]() -> void {
    // Create a thread that we try to kill
    Thread::ThreadId tid;
    Thread::ThreadPtr thread = api_->threadFactory().createThread([this, &tid]() -> void {
      tid = api_->threadFactory().currentThreadId();

      // Prepare signal handler to eat SIGABRT for the child thread.
      struct sigaction saction;
      std::memset(&saction, 0, sizeof(saction));
      saction.sa_flags = SA_SIGINFO;
      saction.sa_sigaction = &handler;
      sigaction(SIGABRT, &saction, nullptr);

      // Signal to test thread that tid has been set.
      {
        absl::MutexLock lock(&mutex_);
        outstanding_notifies_ += 1;
      }

      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    });

    // Wait for child thread to notify of its tid
    {
      absl::MutexLock lock(&mutex_);
      waitForOutstandingNotify();
    }

    // Create vector with child tid and run the action.
    const auto now = api_->timeSource().monotonicTime();
    const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {{tid, now}};

    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::KILL, tid_ltt_pairs, now);
  };

  EXPECT_DEATH(die_function(), "aborting from AbortAction instead");
}

#endif

} // namespace
} // namespace AbortAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
