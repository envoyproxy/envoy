#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_pipeline.h"
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

class TestAsyncState : public TaskGroup {
public:
  using OnCompleteFn = absl::AnyInvocable<void(absl::Status)>;

  TestAsyncState(size_t num_filters, Event::Dispatcher& dispatcher,
                 OnCompleteFn on_complete = nullptr)
      : TaskGroup(dispatcher), on_complete_(std::move(on_complete)), pipeline_(num_filters) {}

  ~TestAsyncState() override { cancel(); }

  using TaskGroup::launchTask;

  void complete(absl::Status status) {
    if (terminated()) {
      return;
    }
    markTerminated();
    if (auto cb = std::move(on_complete_)) {
      cb(std::move(status));
    }
  }

  void fail(absl::Status status) {
    if (terminated()) {
      return;
    }
    auto cb = std::move(on_complete_);
    cancel();
    if (cb != nullptr) {
      cb(std::move(status));
    }
  }

  void cancel() {
    if (terminated()) {
      return;
    }
    on_complete_ = nullptr;
    cancelHandles();
    ++on_cancel_calls_;
    if (external_on_cancel_counter_ != nullptr) {
      ++(*external_on_cancel_counter_);
    }
    pipeline_.closeAndDrain();
  }

  FilterPipeline<int>& pipeline() { return pipeline_; }

  int on_cancel_calls_{0};
  int* external_on_cancel_counter_{nullptr};

private:
  OnCompleteFn on_complete_;
  FilterPipeline<int> pipeline_;
};

class FilterPipelineTest : public testing::Test {
public:
  FilterPipelineTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")) {}

  void drain() {
    for (int i = 0; i < 10; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};

TEST_F(FilterPipelineTest, StageHandoffAndCompletion) {
  int completion_calls = 0;
  absl::Status final_status = absl::UnknownError("not called");

  TestAsyncState state(2, *dispatcher_, [&](absl::Status s) {
    ++completion_calls;
    final_status = std::move(s);
  });

  EXPECT_EQ(state.pipeline().numStages(), 3u);
  EXPECT_FALSE(state.terminated());

  // Stage 0 -> Stage 1 -> Stage 2 (sink)
  state.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await state.pipeline().receive(0));
        if (!item.has_value()) {
          co_return absl::InternalError("unexpected EOF");
        }
        co_return co_await state.pipeline().propagate(0, *item + 10);
      }(),
      [](absl::Status) {});

  state.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await state.pipeline().receive(1));
        if (!item.has_value()) {
          co_return absl::InternalError("unexpected EOF");
        }
        co_return co_await state.pipeline().propagate(1, *item * 2);
      }(),
      [](absl::Status) {});

  int sink_received = 0;
  state.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await state.pipeline().receive(2));
        if (!item.has_value()) {
          co_return absl::InternalError("unexpected EOF");
        }
        sink_received = *item;
        state.complete(absl::OkStatus());
        co_return absl::OkStatus();
      }(),
      [](absl::Status) {});

  EXPECT_TRUE(state.pipeline().stage(0)->tryPush(5));
  drain();

  EXPECT_TRUE(state.terminated());
  EXPECT_EQ(sink_received, 30);
  EXPECT_EQ(completion_calls, 1);
  EXPECT_THAT(final_status, IsOk());

  // Second complete() is a no-op (single-invocation protection).
  state.complete(absl::InternalError("ignored"));
  EXPECT_EQ(completion_calls, 1);
}

TEST_F(FilterPipelineTest, CancelClearsOnCompleteAndClosesStages) {
  int completion_calls = 0;
  TestAsyncState state(1, *dispatcher_, [&](absl::Status) { ++completion_calls; });

  bool saw_eof = false;
  absl::Status task_done_status = absl::OkStatus();
  state.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await state.pipeline().receive(0));
        if (!item.has_value()) {
          saw_eof = true;
        }
        co_return absl::OkStatus();
      }(),
      [&](absl::Status s) { task_done_status = std::move(s); });

  state.cancel();
  EXPECT_TRUE(state.terminated());
  EXPECT_EQ(state.on_cancel_calls_, 1);
  EXPECT_EQ(completion_calls, 0);
  // Cancelled task must unwind with CancelledError, not normal EOF (nullopt).
  EXPECT_FALSE(saw_eof);
  EXPECT_TRUE(absl::IsCancelled(task_done_status));
  EXPECT_TRUE(state.pipeline().stage(0)->closed());
  EXPECT_TRUE(state.pipeline().stage(1)->closed());

  // Calling launchTask() after cancel() does not launch the task.
  bool task_ran_after_cancel = false;
  state.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        task_ran_after_cancel = true;
        co_return absl::OkStatus();
      }(),
      [&](absl::Status) { task_ran_after_cancel = true; });
  EXPECT_FALSE(task_ran_after_cancel);

  // Calling receive() or propagate() on a closed pipeline immediately returns CancelledError.
  TestAsyncState closed_pipeline_state(1, *dispatcher_);
  closed_pipeline_state.pipeline().closeAndDrain();

  absl::Status post_close_rx = absl::OkStatus();
  closed_pipeline_state.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await closed_pipeline_state.pipeline().receive(0));
        (void)item;
        co_return absl::OkStatus();
      }(),
      [&](absl::Status s) { post_close_rx = std::move(s); });
  EXPECT_TRUE(absl::IsCancelled(post_close_rx));

  absl::Status post_close_tx = absl::OkStatus();
  closed_pipeline_state.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        co_return co_await closed_pipeline_state.pipeline().propagate(0, 42);
      }(),
      [&](absl::Status s) { post_close_tx = std::move(s); });
  EXPECT_TRUE(absl::IsCancelled(post_close_tx));

  // Idempotent cancel.
  state.cancel();
  EXPECT_EQ(state.on_cancel_calls_, 1);
}

TEST_F(FilterPipelineTest, FailCancelsAndInvokesCompletion) {
  int completion_calls = 0;
  absl::Status reported_status;

  TestAsyncState state(1, *dispatcher_, [&](absl::Status s) {
    ++completion_calls;
    reported_status = std::move(s);
  });

  state.fail(absl::AbortedError("custom abort"));

  EXPECT_TRUE(state.terminated());
  EXPECT_EQ(state.on_cancel_calls_, 1);
  EXPECT_EQ(completion_calls, 1);
  EXPECT_EQ(reported_status.code(), absl::StatusCode::kAborted);
}

TEST_F(FilterPipelineTest, ReentrantCancelDuringCoroutineFrameDestructionIsSafe) {
  auto state = std::make_shared<TestAsyncState>(2, *dispatcher_);

  struct ReentrantGuard {
    std::shared_ptr<TestAsyncState> state;
    ~ReentrantGuard() {
      // Triggered inside handle.cancel() when coroutine frame is destroyed.
      // Must not corrupt handles_ or re-enter cancel/complete/launchTask unsafely.
      state->cancel();
      state->complete(absl::InternalError("from destructor"));
      state->launchTask([]() -> Coroutine::Task<absl::Status> { co_return absl::OkStatus(); }(),
                        [](absl::Status) {});
    }
  };

  for (size_t i = 0; i < 2; ++i) {
    state->launchTask(
        [state, i]() -> Coroutine::Task<absl::Status> {
          ReentrantGuard guard{state};
          ASSIGN_OR_CO_RETURN(auto item, co_await state->pipeline().receive(i));
          (void)item;
          co_return absl::OkStatus();
        }(),
        [](absl::Status) {});
  }

  state->cancel();
  EXPECT_TRUE(state->terminated());
  EXPECT_EQ(state->on_cancel_calls_, 1);
}

TEST_F(FilterPipelineTest, DestructorCancelsUncancelledPipeline) {
  bool destroyed_cleanly = false;
  int external_cancel_count = 0;
  struct ScopeTracker {
    bool* flag;
    ~ScopeTracker() { *flag = true; }
  };

  {
    TestAsyncState state(1, *dispatcher_);
    state.external_on_cancel_counter_ = &external_cancel_count;
    state.launchTask(
        [&]() -> Coroutine::Task<absl::Status> {
          ScopeTracker tracker{&destroyed_cleanly};
          ASSIGN_OR_CO_RETURN(auto item, co_await state.pipeline().receive(0));
          (void)item;
          co_return absl::OkStatus();
        }(),
        [](absl::Status) {});
    EXPECT_FALSE(destroyed_cleanly);
  }
  EXPECT_TRUE(destroyed_cleanly);
  EXPECT_EQ(external_cancel_count, 1);
}

TEST_F(FilterPipelineTest, CloseAndDrainDestroysBufferedItems) {
  FilterPipeline<std::shared_ptr<int>> pipeline(1);
  auto item = std::make_shared<int>(99);
  EXPECT_TRUE(pipeline.stage(0)->tryPush(item));
  EXPECT_EQ(item.use_count(), 2);

  pipeline.closeAndDrain();
  EXPECT_EQ(item.use_count(), 1);
}

TEST_F(FilterPipelineTest, InlineLaunchSuspendingAfterTriggeringCancelIsCancelled) {
  TestAsyncState state(1, *dispatcher_);
  bool frame_destroyed = false;
  struct FrameTracker {
    bool* flag;
    ~FrameTracker() { *flag = true; }
  };

  state.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        FrameTracker tracker{&frame_destroyed};
        state.cancel();
        ASSIGN_OR_CO_RETURN(auto item, co_await state.pipeline().receive(0));
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
