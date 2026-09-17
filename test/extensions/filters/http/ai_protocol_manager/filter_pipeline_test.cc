#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_pipeline.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::IsOk;

class TestPipeline : public FilterPipeline<int> {
public:
  using FilterPipeline<int>::FilterPipeline;
  using FilterPipeline<int>::takeOnComplete;

  ~TestPipeline() override { cancel(); }

  int on_cancel_calls_{0};
  int* external_on_cancel_counter_{nullptr};

protected:
  void onCancel() override {
    ++on_cancel_calls_;
    if (external_on_cancel_counter_ != nullptr) {
      ++(*external_on_cancel_counter_);
    }
  }
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

  TestPipeline pipeline(2, *dispatcher_, [&](absl::Status s) {
    ++completion_calls;
    final_status = std::move(s);
  });

  EXPECT_EQ(pipeline.numStages(), 3u);
  EXPECT_FALSE(pipeline.terminated());
  EXPECT_NE(pipeline.executor(), nullptr);

  // Stage 0 -> Stage 1 -> Stage 2 (sink)
  pipeline.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(0));
        if (!item.has_value()) {
          co_return absl::InternalError("unexpected EOF");
        }
        co_return co_await pipeline.propagate(0, *item + 10);
      }(),
      [](absl::Status) {});

  pipeline.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(1));
        if (!item.has_value()) {
          co_return absl::InternalError("unexpected EOF");
        }
        co_return co_await pipeline.propagate(1, *item * 2);
      }(),
      [](absl::Status) {});

  int sink_received = 0;
  pipeline.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(2));
        if (!item.has_value()) {
          co_return absl::InternalError("unexpected EOF");
        }
        sink_received = *item;
        pipeline.complete(absl::OkStatus());
        co_return absl::OkStatus();
      }(),
      [](absl::Status) {});

  EXPECT_TRUE(pipeline.stage(0)->tryPush(5));
  drain();

  EXPECT_TRUE(pipeline.terminated());
  EXPECT_EQ(sink_received, 30);
  EXPECT_EQ(completion_calls, 1);
  EXPECT_THAT(final_status, IsOk());

  // Second complete() is a no-op (single-invocation protection).
  pipeline.complete(absl::InternalError("ignored"));
  EXPECT_EQ(completion_calls, 1);
}

TEST_F(FilterPipelineTest, CancelClearsOnCompleteAndClosesStages) {
  int completion_calls = 0;
  TestPipeline pipeline(1, *dispatcher_);
  pipeline.setOnComplete([&](absl::Status) { ++completion_calls; });

  bool saw_eof = false;
  absl::Status task_done_status = absl::OkStatus();
  pipeline.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(0));
        if (!item.has_value()) {
          saw_eof = true;
        }
        co_return absl::OkStatus();
      }(),
      [&](absl::Status s) { task_done_status = std::move(s); });

  pipeline.cancel();
  EXPECT_TRUE(pipeline.terminated());
  EXPECT_EQ(pipeline.on_cancel_calls_, 1);
  EXPECT_EQ(completion_calls, 0);
  // Cancelled task must unwind with CancelledError, not normal EOF (nullopt).
  EXPECT_FALSE(saw_eof);
  EXPECT_TRUE(absl::IsCancelled(task_done_status));
  EXPECT_TRUE(pipeline.stage(0)->closed());
  EXPECT_TRUE(pipeline.stage(1)->closed());

  // Calling receive() or propagate() after cancel() immediately returns CancelledError.
  absl::Status post_cancel_rx = absl::OkStatus();
  pipeline.launchTask(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(0));
        (void)item;
        co_return absl::OkStatus();
      }(),
      [&](absl::Status s) { post_cancel_rx = std::move(s); });
  EXPECT_TRUE(absl::IsCancelled(post_cancel_rx));

  absl::Status post_cancel_tx = absl::OkStatus();
  pipeline.launchTask(
      [&]() -> Coroutine::Task<absl::Status> { co_return co_await pipeline.propagate(0, 42); }(),
      [&](absl::Status s) { post_cancel_tx = std::move(s); });
  EXPECT_TRUE(absl::IsCancelled(post_cancel_tx));

  // Idempotent cancel.
  pipeline.cancel();
  EXPECT_EQ(pipeline.on_cancel_calls_, 1);
}

TEST_F(FilterPipelineTest, TakeOnCompleteBeforeCancel) {
  int completion_calls = 0;
  absl::Status reported_status;

  TestPipeline pipeline(1, *dispatcher_, [&](absl::Status s) {
    ++completion_calls;
    reported_status = std::move(s);
  });

  auto cb = pipeline.takeOnComplete();
  pipeline.cancel();
  ASSERT_NE(cb, nullptr);
  cb(absl::AbortedError("custom abort"));

  EXPECT_EQ(completion_calls, 1);
  EXPECT_EQ(reported_status.code(), absl::StatusCode::kAborted);
}

TEST_F(FilterPipelineTest, ReentrantCancelDuringCoroutineFrameDestructionIsSafe) {
  auto pipeline = std::make_shared<TestPipeline>(2, *dispatcher_);

  struct ReentrantGuard {
    std::shared_ptr<TestPipeline> pipeline;
    ~ReentrantGuard() {
      // Triggered inside handle.cancel() when coroutine frame is destroyed.
      // Must not corrupt handles_ or re-enter cancel/complete/launchTask unsafely.
      pipeline->cancel();
      pipeline->complete(absl::InternalError("from destructor"));
      pipeline->launchTask([]() -> Coroutine::Task<absl::Status> { co_return absl::OkStatus(); }(),
                           [](absl::Status) {});
    }
  };

  for (size_t i = 0; i < 2; ++i) {
    pipeline->launchTask(
        [pipeline, i]() -> Coroutine::Task<absl::Status> {
          ReentrantGuard guard{pipeline};
          ASSIGN_OR_CO_RETURN(auto item, co_await pipeline->receive(i));
          (void)item;
          co_return absl::OkStatus();
        }(),
        [](absl::Status) {});
  }

  pipeline->cancel();
  EXPECT_TRUE(pipeline->terminated());
  EXPECT_EQ(pipeline->on_cancel_calls_, 1);
}

TEST_F(FilterPipelineTest, DestructorCancelsUncancelledPipeline) {
  bool destroyed_cleanly = false;
  int external_cancel_count = 0;
  struct ScopeTracker {
    bool* flag;
    ~ScopeTracker() { *flag = true; }
  };

  {
    TestPipeline pipeline(1, *dispatcher_);
    pipeline.external_on_cancel_counter_ = &external_cancel_count;
    pipeline.launchTask(
        [&]() -> Coroutine::Task<absl::Status> {
          ScopeTracker tracker{&destroyed_cleanly};
          ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(0));
          (void)item;
          co_return absl::OkStatus();
        }(),
        [](absl::Status) {});
    EXPECT_FALSE(destroyed_cleanly);
  }
  EXPECT_TRUE(destroyed_cleanly);
  EXPECT_EQ(external_cancel_count, 1);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
