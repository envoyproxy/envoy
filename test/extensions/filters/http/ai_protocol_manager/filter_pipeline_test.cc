#include <memory>
#include <utility>

#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
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

class FilterPipelineTest : public testing::Test {
public:
  FilterPipelineTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        executor_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)) {}

  Coroutine::DetachedHandle launch(
      Coroutine::Task<absl::Status> task,
      absl::AnyInvocable<void(absl::Status)> on_done = [](absl::Status) {}) {
    return Coroutine::launch(std::move(task), executor_, std::move(on_done),
                             Coroutine::StartMode::Inline);
  }

  void drain() {
    for (int i = 0; i < 10; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
};

TEST_F(FilterPipelineTest, StageHandoffAndEof) {
  FilterPipeline<int> pipeline(2);

  EXPECT_EQ(pipeline.numStages(), 3u);
  EXPECT_FALSE(pipeline.closed());

  // Stage 0 -> Stage 1 -> Stage 2 (sink)
  auto h0 = launch([&]() -> Coroutine::Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(0));
    if (!item.has_value()) {
      co_return absl::InternalError("unexpected EOF");
    }
    CO_RETURN_IF_ERROR(co_await pipeline.propagate(0, *item + 10));
    ASSIGN_OR_CO_RETURN(auto eof, co_await pipeline.receive(0));
    if (eof.has_value()) {
      co_return absl::InternalError("expected EOF");
    }
    pipeline.stage(1)->close();
    co_return absl::OkStatus();
  }());

  auto h1 = launch([&]() -> Coroutine::Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(1));
    if (!item.has_value()) {
      co_return absl::InternalError("unexpected EOF");
    }
    CO_RETURN_IF_ERROR(co_await pipeline.propagate(1, *item * 2));
    ASSIGN_OR_CO_RETURN(auto eof, co_await pipeline.receive(1));
    if (eof.has_value()) {
      co_return absl::InternalError("expected EOF");
    }
    pipeline.stage(2)->close();
    co_return absl::OkStatus();
  }());

  int sink_received = 0;
  bool sink_saw_eof = false;
  absl::Status sink_status = absl::UnknownError("not called");
  auto h2 = launch(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(2));
        if (!item.has_value()) {
          co_return absl::InternalError("unexpected EOF");
        }
        sink_received = *item;
        ASSIGN_OR_CO_RETURN(auto eof, co_await pipeline.receive(2));
        sink_saw_eof = !eof.has_value();
        co_return absl::OkStatus();
      }(),
      [&](absl::Status s) { sink_status = std::move(s); });

  EXPECT_TRUE(pipeline.stage(0)->tryPush(5));
  pipeline.stage(0)->close();
  drain();

  EXPECT_EQ(sink_received, 30);
  EXPECT_TRUE(sink_saw_eof);
  EXPECT_THAT(sink_status, IsOk());
}

TEST_F(FilterPipelineTest, CloseAndDrainDestroysBufferedItems) {
  FilterPipeline<std::shared_ptr<int>> pipeline(1);
  auto item = std::make_shared<int>(99);
  EXPECT_TRUE(pipeline.stage(0)->tryPush(item));
  EXPECT_EQ(item.use_count(), 2);

  pipeline.closeAndDrain();
  EXPECT_TRUE(pipeline.closed());
  EXPECT_TRUE(pipeline.stage(0)->closed());
  EXPECT_TRUE(pipeline.stage(1)->closed());
  EXPECT_EQ(item.use_count(), 1);
}

TEST_F(FilterPipelineTest, ReceiveAndPropagateOnClosedPipelineReturnCancelled) {
  FilterPipeline<int> pipeline(1);
  pipeline.closeAndDrain();
  EXPECT_TRUE(pipeline.closed());

  absl::Status post_close_rx = absl::OkStatus();
  auto h_rx = launch(
      [&]() -> Coroutine::Task<absl::Status> {
        ASSIGN_OR_CO_RETURN(auto item, co_await pipeline.receive(0));
        (void)item;
        co_return absl::OkStatus();
      }(),
      [&](absl::Status s) { post_close_rx = std::move(s); });
  EXPECT_TRUE(absl::IsCancelled(post_close_rx));

  absl::Status post_close_tx = absl::OkStatus();
  auto h_tx = launch(
      [&]() -> Coroutine::Task<absl::Status> { co_return co_await pipeline.propagate(0, 42); }(),
      [&](absl::Status s) { post_close_tx = std::move(s); });
  EXPECT_TRUE(absl::IsCancelled(post_close_tx));
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
