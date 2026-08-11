#include <chrono>
#include <coroutine>
#include <memory>
#include <optional>

#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/task.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Coroutine {
namespace {

// Sleeps for `duration` then returns the sleep's status (ok, or cancelled if the
// scope was cancelled while pending).
Task<absl::Status> sleepThenReturn(std::chrono::milliseconds duration) {
  co_return co_await sleep(duration);
}

class DispatcherExecutorTest : public testing::Test {
public:
  DispatcherExecutorTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        executor_(std::make_shared<DispatcherExecutor>(*dispatcher_)) {}

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<DispatcherExecutor> executor_;
};

// schedule() resumes a handle via the dispatcher's post queue.
TEST_F(DispatcherExecutorTest, ScheduleResumesThroughDispatcher) {
  bool done = false;
  DetachedHandle handle = launch(sleepThenReturn(std::chrono::milliseconds(0)), executor_,
                                 [&done](absl::Status status) {
                                   EXPECT_TRUE(status.ok());
                                   done = true;
                                 });
  // Nothing runs until the dispatcher processes the posted start.
  EXPECT_FALSE(done);
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(1), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(done);
}

// schedule() asserts (in debug builds) that the handle's context executor is this
// one: build a root whose context points at a *different* executor and hand it to
// `executor_`, which must trip the assertion.
TEST_F(DispatcherExecutorTest, ScheduleAssertsHandleBelongsToExecutor) {
  // A second executor on a different dispatcher.
  Api::ApiPtr other_api = Api::createApiForTest(time_system_);
  Event::DispatcherPtr other_dispatcher = other_api->allocateDispatcher("other_thread");
  auto other = std::make_shared<DispatcherExecutor>(*other_dispatcher);

  EXPECT_DEBUG_DEATH(
      {
        // `root` keeps owning the frame (we take a handle via from_promise rather
        // than release()), so it is destroyed at scope exit -- no leak in release
        // builds where the assertion is compiled out.
        Detail::RootTask root = Detail::awaitTaskAndCallOnDone(
            sleepThenReturn(std::chrono::milliseconds(0)), [](absl::Status) {});
        root.promise().context_ =
            std::make_shared<CoroutineContext>(other, std::make_shared<CancellationState>());
        executor_->schedule(
            std::coroutine_handle<Detail::RootTask::promise_type>::from_promise(root.promise()));
      },
      "coroutine scheduled on an executor that does not match its context");
}

// TimerAwaitable completes when simulated time reaches the deadline.
TEST_F(DispatcherExecutorTest, TimerAwaitableFiresAfterDeadline) {
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(sleepThenReturn(std::chrono::milliseconds(50)), executor_,
                                 [&result](absl::Status status) { result = std::move(status); });
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(result.has_value());

  time_system_.advanceTimeAndRun(std::chrono::milliseconds(49), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(result.has_value());

  time_system_.advanceTimeAndRun(std::chrono::milliseconds(1), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());
}

// Cancelling a pending TimerAwaitable disarms the timer and unwinds fail-fast.
TEST_F(DispatcherExecutorTest, CancelDisarmsTimer) {
  std::optional<absl::Status> result;
  DetachedHandle handle = launch(sleepThenReturn(std::chrono::milliseconds(50)), executor_,
                                 [&result](absl::Status status) { result = std::move(status); });
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(result.has_value());

  handle.cancel();
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(absl::IsCancelled(*result));

  // The timer was disarmed: advancing past the deadline delivers nothing new.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(100), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(absl::IsCancelled(*result));
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
