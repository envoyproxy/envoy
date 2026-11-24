#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/extensions/filters/http/ai_protocol_manager/chain_writer.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::IsOk;

class ChainWriterTest : public testing::Test {
protected:
  ChainWriterTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_(*dispatcher_), writer_(bridge_),
        executor_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)) {}

  // Runs until no posted, current-iteration, or next-iteration callbacks remain queued (returning
  // immediately once work completes or parks on a watermark).
  void drain() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

  Coroutine::Task<absl::Status> inject(Buffer::Instance& data) {
    co_return co_await writer_.inject(data);
  }

  Coroutine::DetachedHandle launch(Coroutine::Task<absl::Status> task) {
    return Coroutine::launch(
        std::move(task), executor_, [this](absl::Status status) { result_ = std::move(status); },
        Coroutine::StartMode::Inline);
  }

  std::string injected() { return bridge_.injected_.toString(); }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  // Declared before writer_ so it outlives the objects that reference it.
  FakeBridge bridge_;
  ChainWriter writer_;
  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
  absl::Status result_ = absl::UnknownError("never completed");
};

TEST_F(ChainWriterTest, InjectsTheCallersBytesAndDrainsThem) {
  Buffer::OwnedImpl data;
  data.add("hello");
  Coroutine::DetachedHandle handle = launch(inject(data));
  drain();

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(injected(), "hello");
  EXPECT_EQ(data.length(), 0) << "the caller's buffer was not drained";
}

// Nothing to write means nothing to wait for: the await resolves on the caller's stack, without a
// trip through the dispatcher. Asserting before drain() is what makes that visible.
TEST_F(ChainWriterTest, EmptyInjectCompletesWithoutSuspending) {
  Buffer::OwnedImpl data;
  Coroutine::DetachedHandle handle = launch(inject(data));

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(injected(), "");
}

TEST_F(ChainWriterTest, CancelStopsAPendingInject) {
  Buffer::OwnedImpl data;
  data.add("hello");
  Coroutine::DetachedHandle handle = launch(inject(data));
  handle.cancel();
  drain();

  EXPECT_EQ(result_.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(injected(), "");
}

TEST_F(ChainWriterTest, CoroutineInjectPausesOnWatermarkAndResumesWhenDrained) {
  bridge_.raiseWriteWatermark();

  Buffer::OwnedImpl data;
  data.add("paused-payload");
  Coroutine::DetachedHandle handle = launch(inject(data));
  drain();

  EXPECT_EQ(injected(), "");
  EXPECT_EQ(result_.code(), absl::StatusCode::kUnknown);

  bridge_.lowerWriteWatermark();
  drain();

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(injected(), "paused-payload");
}

// If the watermark rises again before the scheduled write-resume callback runs, the writer re-arms
// rather than injecting into a paused chain.
TEST_F(ChainWriterTest, RearmsWhenWatermarkRisesBeforeScheduledResumeRuns) {
  bridge_.raiseWriteWatermark();

  Buffer::OwnedImpl data;
  data.add("rearmed-payload");
  Coroutine::DetachedHandle handle = launch(inject(data));
  drain();
  EXPECT_EQ(injected(), "");

  // Lower schedules the resume callback for current iteration; raising again before the loop runs
  // exercises the re-arm branch.
  bridge_.lowerWriteWatermark();
  bridge_.raiseWriteWatermark();
  drain();
  EXPECT_EQ(injected(), "");
  EXPECT_EQ(result_.code(), absl::StatusCode::kUnknown);

  bridge_.lowerWriteWatermark();
  drain();
  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(injected(), "rearmed-payload");
}

// Cancelling the coroutine's DetachedHandle while parked on write back-pressure disarms the bridge
// write-resume callback via LeafAwaitable::onCancel() and unwinds the coroutine synchronously.
TEST_F(ChainWriterTest, CancelUnwindsParkedWatermarkCoroutineInject) {
  bridge_.raiseWriteWatermark();

  Buffer::OwnedImpl data;
  data.add("0123456789");
  Coroutine::DetachedHandle handle = launch(inject(data));
  drain();
  EXPECT_EQ(injected(), "");

  handle.cancel();
  drain();

  EXPECT_EQ(result_.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(injected(), "");
}

// Cancelling while parked on an exhausted per-iteration injection budget disarms budget_cb_
// cleanly.
TEST_F(ChainWriterTest, CancelUnwindsBudgetYieldedInject) {
  Buffer::OwnedImpl data(std::string(10 * ChainWriter::WriteChunkSize, 'x'));
  std::optional<Coroutine::DetachedHandle> handle;
  bridge_.on_inject_ = [&]() {
    if (bridge_.inject_calls_ == 8) {
      // Current-iteration post runs after chunk 9 parks on budget_cb_ (next iteration) and before
      // the next loop iteration executes.
      dispatcher_->post([&]() {
        if (handle.has_value()) {
          handle->cancel();
        }
      });
    }
  };
  handle = launch(inject(data));
  drain();

  EXPECT_EQ(result_.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(bridge_.inject_calls_, 8);
}

// Re-entrant cancellation inside bridge_.injectData() resolves InjectChunk with CancelledError
// without emitting further chunks.
TEST_F(ChainWriterTest, ReentrantCancelDuringInjectStopsImmediately) {
  Buffer::OwnedImpl data(std::string(3 * ChainWriter::WriteChunkSize, 'z'));
  std::optional<Coroutine::DetachedHandle> handle;
  bridge_.on_inject_ = [&]() {
    if (handle.has_value()) {
      handle->cancel();
    }
  };
  handle = launch(inject(data));
  drain();

  EXPECT_EQ(result_.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(bridge_.inject_calls_, 1);
}

// Even when on-stack cancellation occurs during the final (or only) chunk of inject(), InjectChunk
// surfaces CancelledError rather than returning OkStatus().
TEST_F(ChainWriterTest, ReentrantCancelOnFinalChunkReturnsCancelled) {
  Buffer::OwnedImpl data("single-chunk");
  std::optional<Coroutine::DetachedHandle> handle;
  bridge_.on_inject_ = [&]() {
    if (handle.has_value()) {
      handle->cancel();
    }
  };
  handle = launch(inject(data));
  drain();

  EXPECT_EQ(result_.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(bridge_.inject_calls_, 1);
}

TEST_F(ChainWriterTest, ReusesSchedulableCallbackAcrossBudgetYieldsAndWatermarkPauses) {
  struct FakeSchedulableCallback : public Event::SchedulableCallback {
    FakeSchedulableCallback(std::deque<Event::PostCb>& q, std::function<void()> cb)
        : queue(q), callback(std::move(cb)) {}
    void scheduleCallbackCurrentIteration() override { enqueue(); }
    void scheduleCallbackNextIteration() override { enqueue(); }
    void cancel() override { is_enabled = false; }
    bool enabled() override { return is_enabled; }
    void enqueue() {
      if (is_enabled) {
        return;
      }
      is_enabled = true;
      queue.push_back([this]() {
        if (is_enabled) {
          is_enabled = false;
          callback();
        }
      });
    }
    std::deque<Event::PostCb>& queue;
    std::function<void()> callback;
    bool is_enabled{false};
  };

  NiceMock<Event::MockDispatcher> mock_dispatcher;
  uint32_t created_schedulable_cbs = 0;
  std::deque<Event::PostCb> posted;
  ON_CALL(mock_dispatcher, post(testing::_)).WillByDefault([&posted](Event::PostCb cb) {
    posted.push_back(std::move(cb));
  });
  ON_CALL(mock_dispatcher, createSchedulableCallback_(testing::_))
      .WillByDefault([&](std::function<void()> cb) -> Event::SchedulableCallback* {
        ++created_schedulable_cbs;
        return new FakeSchedulableCallback(posted, std::move(cb));
      });

  FakeBridge mock_bridge(mock_dispatcher);
  ChainWriter writer(mock_bridge);
  auto executor = std::make_shared<Coroutine::DispatcherExecutor>(mock_dispatcher);

  // 20 chunks (> 2 * InjectChunksPerIteration) + watermark pause/resume across two inject() calls
  // must allocate at most two SchedulableCallbacks (one for write_resume_cb_, one for budget_cb_)
  // for the writer's lifetime.
  const std::string part1(20 * ChainWriter::WriteChunkSize, 'a');
  const std::string part2(10 * ChainWriter::WriteChunkSize, 'b');
  mock_bridge.raiseWriteWatermark();

  absl::Status status = absl::UnknownError("pending");
  auto run_two_injects = [](ChainWriter& w, std::string p1,
                            std::string p2) -> Coroutine::Task<absl::Status> {
    Buffer::OwnedImpl b1(p1);
    if (absl::Status s = co_await w.inject(b1); !s.ok()) {
      co_return s;
    }
    Buffer::OwnedImpl b2(p2);
    co_return co_await w.inject(b2);
  };
  Coroutine::DetachedHandle h = Coroutine::launch(
      run_two_injects(writer, part1, part2), executor,
      [&status](absl::Status s) { status = std::move(s); }, Coroutine::StartMode::Inline);

  while (!posted.empty()) {
    Event::PostCb cb = std::move(posted.front());
    posted.pop_front();
    cb();
  }
  EXPECT_EQ(mock_bridge.injected_.length(), 0);

  mock_bridge.lowerWriteWatermark();
  while (!posted.empty()) {
    Event::PostCb cb = std::move(posted.front());
    posted.pop_front();
    cb();
  }

  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(mock_bridge.injected_.toString(), part1 + part2);
  EXPECT_EQ(created_schedulable_cbs, 2);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
