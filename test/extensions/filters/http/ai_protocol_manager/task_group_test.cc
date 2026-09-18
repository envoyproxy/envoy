#include <memory>
#include <utility>

#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/task_group.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::IsOk;

class TestTaskGroup : public TaskGroup {
public:
  explicit TestTaskGroup(Event::Dispatcher& dispatcher) : TaskGroup(dispatcher) {}

  using TaskGroup::cancelHandles;
  using TaskGroup::launchTask;
  using TaskGroup::markTerminated;
};

class TaskGroupTest : public testing::Test {
public:
  TaskGroupTest() : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")) {}

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};

TEST_F(TaskGroupTest, LaunchAndMarkTerminated) {
  Coroutine::AsyncQueue<int> queue(/*max_size=*/1);
  bool post_terminated_task_completed = false;

  {
    TestTaskGroup group(*dispatcher_);
    EXPECT_FALSE(group.terminated());

    absl::Status done_status = absl::UnknownError("not called");
    group.launchTask([]() -> Coroutine::Task<absl::Status> { co_return absl::OkStatus(); }(),
                     [&](absl::Status s) { done_status = std::move(s); });
    EXPECT_THAT(done_status, IsOk());

    // Launch a task that stays suspended across ~TestTaskGroup() after markTerminated().
    group.launchTask(
        [&]() -> Coroutine::Task<absl::Status> {
          ASSIGN_OR_CO_RETURN(auto item, co_await queue.pop());
          (void)item;
          post_terminated_task_completed = true;
          co_return absl::OkStatus();
        }(),
        [](absl::Status) {});

    group.markTerminated();
    EXPECT_TRUE(group.terminated());

    // Launching after markTerminated() is a no-op.
    bool ran_after_terminate = false;
    group.launchTask(
        [&]() -> Coroutine::Task<absl::Status> {
          ran_after_terminate = true;
          co_return absl::OkStatus();
        }(),
        [&](absl::Status) { ran_after_terminate = true; });
    EXPECT_FALSE(ran_after_terminate);
  }

  // Because markTerminated() was called, ~TaskGroup() did not cancel the suspended task;
  // it can finish naturally when its awaited operation completes.
  EXPECT_FALSE(post_terminated_task_completed);
  EXPECT_TRUE(queue.tryPush(1));
  EXPECT_TRUE(post_terminated_task_completed);
}

TEST_F(TaskGroupTest, CancelHandlesUnwindsSuspendedTasks) {
  TestTaskGroup group(*dispatcher_);
  Coroutine::AsyncQueue<int> queue(/*max_size=*/1);

  bool saw_eof = false;
  absl::Status task_done_status = absl::OkStatus();
  group.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await queue.pop());
        if (!item.has_value()) {
          saw_eof = true;
        }
        co_return absl::OkStatus();
      }(),
      [&](absl::Status s) { task_done_status = std::move(s); });

  group.cancelHandles();
  EXPECT_TRUE(group.terminated());
  EXPECT_FALSE(saw_eof);
  EXPECT_TRUE(absl::IsCancelled(task_done_status));

  // Launching after cancelHandles() is a no-op.
  bool task_ran_after_cancel = false;
  group.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        task_ran_after_cancel = true;
        co_return absl::OkStatus();
      }(),
      [&](absl::Status) { task_ran_after_cancel = true; });
  EXPECT_FALSE(task_ran_after_cancel);
}

TEST_F(TaskGroupTest, ReentrantCancelDuringCoroutineFrameDestructionIsSafe) {
  auto group = std::make_shared<TestTaskGroup>(*dispatcher_);
  Coroutine::AsyncQueue<int> q0(/*max_size=*/1);
  Coroutine::AsyncQueue<int> q1(/*max_size=*/1);

  struct ReentrantGuard {
    std::shared_ptr<TestTaskGroup> group;
    ~ReentrantGuard() {
      // Triggered inside handle.cancel() when coroutine frame is destroyed.
      // Must not corrupt handles_ or re-enter cancelHandles/markTerminated/launchTask unsafely.
      group->cancelHandles();
      group->markTerminated();
      group->launchTask([]() -> Coroutine::Task<absl::Status> { co_return absl::OkStatus(); }(),
                        [](absl::Status) {});
    }
  };

  group->launchTask(
      [group, &q0]() -> Coroutine::Task<absl::Status> {
        ReentrantGuard guard{group};
        ASSIGN_OR_CO_RETURN(auto item, co_await q0.pop());
        (void)item;
        co_return absl::OkStatus();
      }(),
      [](absl::Status) {});

  group->launchTask(
      [group, &q1]() -> Coroutine::Task<absl::Status> {
        ReentrantGuard guard{group};
        ASSIGN_OR_CO_RETURN(auto item, co_await q1.pop());
        (void)item;
        co_return absl::OkStatus();
      }(),
      [](absl::Status) {});

  group->cancelHandles();
  EXPECT_TRUE(group->terminated());
}

TEST_F(TaskGroupTest, DestructorCancelsUnterminatedTasks) {
  bool destroyed_cleanly = false;
  Coroutine::AsyncQueue<int> queue(/*max_size=*/1);
  struct ScopeTracker {
    bool* flag;
    ~ScopeTracker() { *flag = true; }
  };

  {
    TestTaskGroup group(*dispatcher_);
    group.launchTask(
        [&]() -> Coroutine::Task<absl::Status> {
          ScopeTracker tracker{&destroyed_cleanly};
          ASSIGN_OR_CO_RETURN(auto item, co_await queue.pop());
          (void)item;
          co_return absl::OkStatus();
        }(),
        [](absl::Status) {});
    EXPECT_FALSE(destroyed_cleanly);
  }
  EXPECT_TRUE(destroyed_cleanly);
}

TEST_F(TaskGroupTest, InlineLaunchSuspendingAfterTriggeringCancelIsCancelled) {
  TestTaskGroup group(*dispatcher_);
  Coroutine::AsyncQueue<int> queue(/*max_size=*/1);
  bool frame_destroyed = false;
  struct FrameTracker {
    bool* flag;
    ~FrameTracker() { *flag = true; }
  };

  group.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        FrameTracker tracker{&frame_destroyed};
        group.cancelHandles();
        ASSIGN_OR_CO_RETURN(auto item, co_await queue.pop());
        (void)item;
        co_return absl::OkStatus();
      }(),
      [](absl::Status) {});

  EXPECT_TRUE(frame_destroyed);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
