#include <csignal>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/thread/thread.h"
#include "envoy/watchdog/v3/abort_action.pb.h"

#include "source/common/watchdog/abort_action.h"
#include "source/common/watchdog/abort_action_config.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Watchdog {
namespace {

using AbortActionConfig = envoy::watchdog::v3::AbortActionConfig;

class AbortActionTest : public testing::Test {
protected:
  AbortActionTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        context_({*api_, *dispatcher_, *stats_.rootScope(), "test"}) {}

  Stats::TestUtil::TestStore stats_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Server::Configuration::GuardDogActionFactoryContext context_;
  std::unique_ptr<Server::Configuration::GuardDogAction> action_;

  // Used to synchronize with the main thread
  absl::Notification child_ready_;
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

TEST_F(AbortActionTest, ShouldKillTheProcess) {
  AbortActionConfig config;
  config.mutable_wait_duration()->set_seconds(1);
  action_ = std::make_unique<AbortAction>(config, context_);

  auto die_function = [this]() -> void {
    // Create a thread that we'll kill
    Thread::ThreadId tid;
    Thread::ThreadPtr thread = api_->threadFactory().createThread([this, &tid]() -> void {
      tid = api_->threadFactory().currentThreadId();

      child_ready_.Notify();

      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    });

    child_ready_.WaitForNotification();

    // Create vector with child tid and run the action.
    const auto now = api_->timeSource().monotonicTime();
    const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {{tid, now}};

    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::KILL, tid_ltt_pairs, now);
  };

  EXPECT_DEATH(die_function(), "");
}

#ifndef WIN32
// insufficient signal support on Windows.
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

      child_ready_.Notify();

      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    });

    child_ready_.WaitForNotification();

    // Create vector with child tid and run the action.
    const auto now = api_->timeSource().monotonicTime();
    const std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {{tid, now}};

    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::KILL, tid_ltt_pairs, now);
  };

  EXPECT_DEATH(die_function(), "aborting from Watchdog AbortAction instead");
}
#endif

} // namespace
} // namespace Watchdog
} // namespace Envoy
