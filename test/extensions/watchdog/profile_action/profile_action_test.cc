#include <memory>

#include "envoy/common/time.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/watchdog/profile_action/v3alpha/profile_action.pb.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/thread/thread.h"

#include "common/common/assert.h"
#include "common/filesystem/directory.h"
#include "common/profiler/profiler.h"

#include "extensions/watchdog/profile_action/config.h"
#include "extensions/watchdog/profile_action/profile_action.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace ProfileAction {
namespace {

class ProfileActionTest : public testing::Test {
protected:
  ProfileActionTest()
      : time_system_(std::make_unique<Event::SimulatedTimeSystem>()),
        api_(Api::createApiForTest(*time_system_)), dispatcher_(api_->allocateDispatcher("test")),
        context_({*api_, *dispatcher_}), test_path_(generateTestPath()) {}

  // Generates a unique path for a testcase.
  static std::string generateTestPath() {
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();

    std::string test_path = TestEnvironment::temporaryPath(
        absl::StrJoin({test_info->test_suite_name(), test_info->name()}, "/"));
    TestEnvironment::createPath(test_path);

    return test_path;
  }

  // Counts the number of non-empty profiles found within a directory.
  int countNumberOfProfileInPath(const std::string& path) {
    int nonempty_profiles_found = 0;
    Filesystem::Directory directory(path);

    for (const Filesystem::DirectoryEntry& entry : directory) {
      const std::string full_path = path + "/" + entry.name_;

      // Count if its a non-empty file with the prefix of profiles.
      if (entry.type_ == Filesystem::FileType::Regular &&
          absl::StartsWith(entry.name_, "ProfileAction") &&
          api_->fileSystem().fileSize(full_path) > 0) {
        nonempty_profiles_found++;
      }
    }

    return nonempty_profiles_found;
  }

  void waitForOutstandingNotify() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    mutex_.Await(absl::Condition(
        +[](int* outstanding_notifies) -> bool { return *outstanding_notifies > 0; },
        &outstanding_notifies_));
    outstanding_notifies_ -= 1;
  }

  std::unique_ptr<Event::TestTimeSystem> time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Server::Configuration::GuardDogActionFactoryContext context_;
  std::unique_ptr<Server::Configuration::GuardDogAction> action_;
  // Path for the test case to dump any profiles to.
  const std::string test_path_;
  // Used to synchronize with the dispatch thread
  absl::Mutex mutex_;
  int outstanding_notifies_ ABSL_GUARDED_BY(mutex_) = 0;
};

TEST_F(ProfileActionTest, CanDoSingleProfile) {
  // Create configuration.
  envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig config;
  config.set_profile_path(test_path_);
  config.mutable_profile_duration()->set_seconds(1);

  // Create the ProfileAction before we start running the dispatcher
  // otherwise the timer created will in ProfileActions ctor will
  // not be thread safe.
  action_ = std::make_unique<ProfileAction>(config, context_);
  Thread::ThreadPtr thread = api_->threadFactory().createThread(
      [this]() -> void { dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit); });

  // Create vector of relevant threads
  const auto now = api_->timeSource().monotonicTime();
  std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {
      {Thread::ThreadId(10), now}};

  // Check that we can do at least a single profile
  dispatcher_->post([&tid_ltt_pairs, &now, this]() -> void {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
    absl::MutexLock lock(&mutex_);
    outstanding_notifies_ += 1;
  });

  absl::MutexLock lock(&mutex_);
  waitForOutstandingNotify();
  time_system_->advanceTimeWait(std::chrono::seconds(2));

  dispatcher_->exit();
  thread->join();

#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 1);
#else
  // Profiler won't run in this case, so there should be no files generated.
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 0);
#endif
}

TEST_F(ProfileActionTest, CanDoMultipleProfiles) {
  // Create configuration.
  envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig config;
  config.set_profile_path(test_path_);
  config.mutable_profile_duration()->set_seconds(1);
  // Create the ProfileAction before we start running the dispatcher
  // otherwise the timer created will in ProfileActions ctor will
  // not be thread safe.
  action_ = std::make_unique<ProfileAction>(config, context_);
  Thread::ThreadPtr thread = api_->threadFactory().createThread(
      [this]() -> void { dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit); });

  // Create vector of relevant threads
  const auto now = api_->timeSource().monotonicTime();
  std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {
      {Thread::ThreadId(10), now}};

  // Check that we can do at least a single profile
  dispatcher_->post([&tid_ltt_pairs, &now, this]() -> void {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
    absl::MutexLock lock(&mutex_);
    outstanding_notifies_ += 1;
  });

  absl::MutexLock lock(&mutex_);
  waitForOutstandingNotify();
  time_system_->advanceTimeWait(std::chrono::seconds(2));

#ifdef PROFILER_AVAILABLE
  ASSERT_EQ(countNumberOfProfileInPath(test_path_), 1);
#else
  // Profiler won't run in this case, so there should be no files generated.
  ASSERT_EQ(countNumberOfProfileInPath(test_path_), 0);
#endif

  // Check we can do multiple profiles
  dispatcher_->post([&tid_ltt_pairs, &now, this]() -> void {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
    absl::MutexLock lock(&mutex_);
    outstanding_notifies_ += 1;
  });

  waitForOutstandingNotify();
  time_system_->advanceTimeWait(std::chrono::seconds(2));

  dispatcher_->exit();
  thread->join();

#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 2);
#else
  // Profiler won't run in this case, so there should be no files generated.
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 0);
#endif
}

TEST_F(ProfileActionTest, CannotTriggerConcurrentProfiles) {
  // Create configuration.
  envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig config;
  TestUtility::loadFromJson(absl::Substitute(R"EOF({ "profile_path": "$0", })EOF", test_path_),
                            config);
  // Create the ProfileAction before we start running the dispatcher
  // otherwise the timer created will in ProfileActions ctor will
  // not be thread safe.
  action_ = std::make_unique<ProfileAction>(config, context_);
  Thread::ThreadPtr thread = api_->threadFactory().createThread(
      [this]() -> void { dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit); });

  // Create vector of relevant threads
  const auto now = api_->timeSource().monotonicTime();
  std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {
      {Thread::ThreadId(10), now}};

  dispatcher_->post([&, this]() -> void {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);

    // This subsequent call should fail since the one prior starts a profile.
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);

    absl::MutexLock lock(&mutex_);
    outstanding_notifies_ += 1;
  });

  absl::MutexLock lock(&mutex_);
  waitForOutstandingNotify();
  time_system_->advanceTimeWait(std::chrono::seconds(6));

  dispatcher_->exit();
  thread->join();
#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 1);
#else
  // Profiler won't run in this case, so there should be no files generated.
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 0);
#endif
}

TEST_F(ProfileActionTest, ShouldNotProfileIfDirectoryDoesNotExist) {
  // Create configuration.
  envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig config;
  const std::string nonexistant_path = test_path_ + "/nonexistant_dir/";
  TestUtility::loadFromJson(
      absl::Substitute(R"EOF({ "profile_path": "$0", })EOF", nonexistant_path), config);
  // Create the ProfileAction before we start running the dispatcher
  // otherwise the timer created will in ProfileActions ctor will
  // not be thread safe.
  action_ = std::make_unique<ProfileAction>(config, context_);
  Thread::ThreadPtr thread = api_->threadFactory().createThread(
      [this]() -> void { dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit); });

  // Create vector of relevant threads
  const auto now = api_->timeSource().monotonicTime();
  std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {
      {Thread::ThreadId(10), now}};

  dispatcher_->post([&, this]() -> void {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
    absl::MutexLock lock(&mutex_);
    outstanding_notifies_ += 1;
  });

  absl::MutexLock lock(&mutex_);
  waitForOutstandingNotify();
  time_system_->advanceTimeWait(std::chrono::seconds(6));

  dispatcher_->exit();
  thread->join();

  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 0);
  EXPECT_FALSE(api_->fileSystem().directoryExists(nonexistant_path));
}

TEST_F(ProfileActionTest, ShouldNotProfileIfNoTids) {
  // Create configuration.
  envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig config;
  TestUtility::loadFromJson(absl::Substitute(R"EOF({ "profile_path": "$0"})EOF", test_path_),
                            config);
  // Create the ProfileAction before we start running the dispatcher
  // otherwise the timer created will in ProfileActions ctor will
  // not be thread safe.
  action_ = std::make_unique<ProfileAction>(config, context_);
  Thread::ThreadPtr thread = api_->threadFactory().createThread(
      [this]() -> void { dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit); });

  // Test that no profiles are created given empty vector of valid TIDs
  dispatcher_->post([this]() -> void {
    std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs;
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs,
                 api_->timeSource().monotonicTime());
    absl::MutexLock lock(&mutex_);
    outstanding_notifies_ += 1;
  });

  absl::MutexLock lock(&mutex_);
  waitForOutstandingNotify();
  time_system_->advanceTimeWait(std::chrono::seconds(2));

  dispatcher_->exit();
  thread->join();

  // No profiles should have been created
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 0);
}

TEST_F(ProfileActionTest, ShouldSaturateTids) {
  // Create configuration that we'll run until it saturates.
  envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig config;
  config.set_profile_path(test_path_);
  config.mutable_profile_duration()->set_seconds(1);
  config.set_max_profiles_per_thread(1);

  // Create the ProfileAction before we start running the dispatcher
  // otherwise the timer created will in ProfileActions ctor will
  // not be thread safe.
  action_ = std::make_unique<ProfileAction>(config, context_);
  Thread::ThreadPtr thread = api_->threadFactory().createThread(
      [this]() -> void { dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit); });

  // Create vector of relevant threads
  const auto now = api_->timeSource().monotonicTime();
  std::vector<std::pair<Thread::ThreadId, MonotonicTime>> tid_ltt_pairs = {
      {Thread::ThreadId(10), now}};

  dispatcher_->post([&, this]() -> void {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
    absl::MutexLock lock(&mutex_);
    outstanding_notifies_ += 1;
  });

  absl::MutexLock lock(&mutex_);
  waitForOutstandingNotify();
  time_system_->advanceTimeWait(std::chrono::seconds(2));

  // check that the profile is created!
#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 1);
#else
  // Profiler won't run in this case, so there should be no files generated.
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 0);
#endif

  // Do another run of the watchdog action. It shouldn't have run again.
  dispatcher_->post([&, this]() -> void {
    action_->run(envoy::config::bootstrap::v3::Watchdog::WatchdogAction::MISS, tid_ltt_pairs, now);
    absl::MutexLock lock(&mutex_);
    outstanding_notifies_ += 1;
  });

  waitForOutstandingNotify();

  // If the callback had scheduled (it shouldn't as we've saturated the profile
  // count) advancing time to make it run.
  time_system_->advanceTimeWait(std::chrono::seconds(2));
  dispatcher_->exit();
  thread->join();

#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 1);
#else
  // Profiler won't run in this case, so there should be no files generated.
  EXPECT_EQ(countNumberOfProfileInPath(test_path_), 0);
#endif
}

} // namespace
} // namespace ProfileAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
