#include <memory>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/async_queue.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/response_filter_manager.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::IsOk;

// Forwards every frame untouched, and records how many it saw.
class CountingSseFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive,
                                          SseStreamPropagator propagate) override {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto event, co_await receive());
      if (!event.has_value()) {
        co_return absl::OkStatus();
      }
      ++seen_;
      CO_RETURN_IF_ERROR(co_await propagate(std::move(*event)));
    }
  }

  int seen_{0};
};

// Stamps a field into every JSON frame, so mutations can be observed on the wire.
class TaggingSseFilter : public AiFilter {
public:
  explicit TaggingSseFilter(std::string tag) : tag_(std::move(tag)) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive,
                                          SseStreamPropagator propagate) override {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto event, co_await receive());
      if (!event.has_value()) {
        co_return absl::OkStatus();
      }
      if ((*event)->is_json()) {
        (*event)->json().json()[tag_] = true;
      }
      CO_RETURN_IF_ERROR(co_await propagate(std::move(*event)));
    }
  }

  std::string tag_;
};

// Forwards `limit` frames and then bows out, which should splice it out of the chain.
class BowOutSseFilter : public AiFilter {
public:
  explicit BowOutSseFilter(int limit) : limit_(limit) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive,
                                          SseStreamPropagator propagate) override {
    for (int i = 0; i < limit_; ++i) {
      ASSIGN_OR_CO_RETURN(auto event, co_await receive());
      if (!event.has_value()) {
        co_return absl::OkStatus();
      }
      ++seen_;
      CO_RETURN_IF_ERROR(co_await propagate(std::move(*event)));
    }
    co_return absl::OkStatus();
  }

  int limit_;
  int seen_{0};
};

// Takes one frame and returns without forwarding it, dropping it on purpose.
class DroppingSseFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive, SseStreamPropagator) override {
    ASSIGN_OR_CO_RETURN(auto event, co_await receive());
    (void)event;
    co_return absl::OkStatus();
  }
};

// Forwards a null frame, which the chain must reject rather than pass to the serializer.
class NullPropagatingSseFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive,
                                          SseStreamPropagator propagate) override {
    ASSIGN_OR_CO_RETURN(auto event, co_await receive());
    if (!event.has_value()) {
      // End of stream. Propagating here would trip the null check during teardown rather than
      // during the test body.
      co_return absl::OkStatus();
    }
    co_return co_await propagate(nullptr);
  }
};

// Joins each pair of frames into one, so frames out are half of frames in.
class MergingSseFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive,
                                          SseStreamPropagator propagate) override {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto first, co_await receive());
      if (!first.has_value()) {
        co_return absl::OkStatus();
      }
      ASSIGN_OR_CO_RETURN(auto second, co_await receive());
      if (second.has_value()) {
        (*first)->raw_data().add("+");
        (*first)->raw_data().add((*second)->raw_data_as_string());
      }
      CO_RETURN_IF_ERROR(co_await propagate(std::move(*first)));
    }
  }
};

class FailingSseFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive, SseStreamPropagator) override {
    ASSIGN_OR_CO_RETURN(auto event, co_await receive());
    (void)event;
    co_return absl::InternalError("filter exploded");
  }
};

// A filter that implements neither encode hook, so it is spliced out before the first frame.
class InertFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }
};

class ResponseFilterManagerTest : public testing::Test {
public:
  ResponseFilterManagerTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")) {}

  ~ResponseFilterManagerTest() override {
    manager_.reset();
    if (out_buffer_manager_ != nullptr) {
      out_buffer_manager_->onDestroy();
    }
  }

  void drain() {
    for (int i = 0; i < 40; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  void makeManager(std::vector<AiFilterSharedPtr> filters,
                   ResponseFilterManager::Config config = ResponseFilterManager::Config{},
                   uint32_t buffer_limit = 1024 * 1024) {
    bridge_ = std::make_unique<FakeBridge>(*dispatcher_, buffer_limit);
    out_buffer_manager_ =
        std::make_unique<BufferManager>(BufferManager::Config{}, factory_, *bridge_);
    manager_ = std::make_unique<ResponseFilterManager>(
        std::move(filters), factory_, *bridge_, *out_buffer_manager_,
        [this](absl::Status status) {
          ++complete_calls_;
          result_ = std::move(status);
        },
        config);
  }

  void feed(absl::string_view body, bool end_stream = true) {
    Buffer::OwnedImpl buf;
    buf.add(body);
    manager_->onData(buf, end_stream);
    drain();
  }

  std::string output() const { return bridge_->injected_.toString(); }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  std::unique_ptr<FakeBridge> bridge_;
  std::unique_ptr<BufferManager> out_buffer_manager_;
  std::unique_ptr<ResponseFilterManager> manager_;

  int complete_calls_{0};
  absl::Status result_ = absl::UnknownError("never completed");
};

TEST_F(ResponseFilterManagerTest, SseNoFiltersPassesFramesThrough) {
  makeManager({});
  feed("data: {\"a\":1}\n\ndata: [DONE]\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(complete_calls_, 1);
  EXPECT_EQ(output(), "data: {\"a\":1}\n\ndata: [DONE]\n\n");
}

TEST_F(ResponseFilterManagerTest, SseFilterSeesEveryFrame) {
  auto filter = std::make_shared<CountingSseFilter>();
  CountingSseFilter* raw = filter.get();
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::move(filter));
  makeManager(std::move(filters));

  feed("data: {\"a\":1}\n\ndata: {\"b\":2}\n\ndata: [DONE]\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(raw->seen_, 3);
  EXPECT_EQ(output(), "data: {\"a\":1}\n\ndata: {\"b\":2}\n\ndata: [DONE]\n\n");
}

TEST_F(ResponseFilterManagerTest, SseMutationsReachTheWire) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TaggingSseFilter>("tagged"));
  makeManager(std::move(filters));

  feed("data: {\"a\":1}\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(output(), "data: {\"a\":1,\"tagged\":true}\n\n");
}

// The chain decodes and reserializes rather than forwarding bytes, so a field it does not model
// only reaches the client if it is carried deliberately.
TEST_F(ResponseFilterManagerTest, SseUnknownFieldsSurviveTheChain) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TaggingSseFilter>("tagged"));
  makeManager(std::move(filters));

  feed("x-provider-trace: abc\ndata: {\"a\":1}\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(output(), "x-provider-trace: abc\ndata: {\"a\":1,\"tagged\":true}\n\n");
}

TEST_F(ResponseFilterManagerTest, SseFiltersRunInOrder) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TaggingSseFilter>("first"));
  filters.push_back(std::make_unique<TaggingSseFilter>("second"));
  makeManager(std::move(filters));

  feed("data: {}\n\n");

  EXPECT_THAT(result_, IsOk());
  const nlohmann::json parsed = nlohmann::json::parse(output().substr(6, output().size() - 8));
  EXPECT_TRUE(parsed["first"]);
  EXPECT_TRUE(parsed["second"]);
}

// A filter that returns cleanly is spliced out; the stream must keep flowing through the rest.
TEST_F(ResponseFilterManagerTest, SseEarlyReturnSplicesFilterOut) {
  auto bower = std::make_unique<BowOutSseFilter>(1);
  BowOutSseFilter* bower_raw = bower.get();
  auto counter = std::make_unique<CountingSseFilter>();
  CountingSseFilter* counter_raw = counter.get();
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::move(bower));
  filters.push_back(std::move(counter));
  makeManager(std::move(filters));

  feed("data: {\"n\":1}\n\ndata: {\"n\":2}\n\ndata: {\"n\":3}\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(bower_raw->seen_, 1);
  // The filter behind it still sees everything.
  EXPECT_EQ(counter_raw->seen_, 3);
  EXPECT_EQ(output(), "data: {\"n\":1}\n\ndata: {\"n\":2}\n\ndata: {\"n\":3}\n\n");
}

// A filter that implements no encode hook must not stall the stream.
TEST_F(ResponseFilterManagerTest, SseInertFilterIsSplicedOutImmediately) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<InertFilter>());
  filters.push_back(std::make_unique<TaggingSseFilter>("tagged"));
  makeManager(std::move(filters));

  feed("data: {\"a\":1}\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(output(), "data: {\"a\":1,\"tagged\":true}\n\n");
}

// A filter owns the frame boundaries it emits, so a frame it takes and does not forward is
// dropped rather than treated as an error. The stream carries on without it.
TEST_F(ResponseFilterManagerTest, SseFilterMayDropAFrameItTook) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<DroppingSseFilter>());
  makeManager(std::move(filters));

  feed("data: a\n\ndata: b\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(complete_calls_, 1);
  EXPECT_EQ(output(), "data: b\n\n");
}

// EXPECT_ENVOY_BUG is a death test where debug assertions are on, so the status is asserted
// inside the statement -- in that mode the process is gone before control returns.
TEST_F(ResponseFilterManagerTest, SsePropagatingANullFrameFailsTheStream) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<NullPropagatingSseFilter>());
  makeManager(std::move(filters));

  EXPECT_ENVOY_BUG(
      {
        feed("data: a\n\n");
        EXPECT_EQ(result_.code(), absl::StatusCode::kInvalidArgument);
      },
      "null SseEventPtr");
}

// Frames in need not match frames out: two frames may leave as one.
TEST_F(ResponseFilterManagerTest, SseFilterMayMergeFrames) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<MergingSseFilter>());
  makeManager(std::move(filters));

  feed("data: a\n\ndata: b\n\ndata: c\n\ndata: d\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(output(), "data: a+b\n\ndata: c+d\n\n");
}

TEST_F(ResponseFilterManagerTest, SseFilterErrorFailsTheStream) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<FailingSseFilter>());
  makeManager(std::move(filters));

  feed("data: {\"a\":1}\n\n");

  EXPECT_EQ(result_.code(), absl::StatusCode::kInternal);
  EXPECT_EQ(complete_calls_, 1);
}

// Frame boundaries have nothing to do with TCP segment boundaries.
TEST_F(ResponseFilterManagerTest, SseInputSplitAcrossManyChunks) {
  auto filter = std::make_shared<CountingSseFilter>();
  CountingSseFilter* raw = filter.get();
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::move(filter));
  makeManager(std::move(filters));

  const std::string body = "data: {\"a\":1}\n\ndata: {\"b\":2}\n\n";
  for (size_t i = 0; i < body.size(); ++i) {
    feed(body.substr(i, 1), /*end_stream=*/i + 1 == body.size());
  }

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(raw->seen_, 2);
  EXPECT_EQ(output(), body);
}

TEST_F(ResponseFilterManagerTest, SseMetadataIsPreserved) {
  makeManager({});
  feed("event: delta\nid: 7\nretry: 500\ndata: {\"a\":1}\n\n");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(output(), "event: delta\nid: 7\nretry: 500\ndata: {\"a\":1}\n\n");
}

TEST_F(ResponseFilterManagerTest, SseEmptyBodyCompletesCleanly) {
  makeManager({});
  feed("");

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(output(), "");
}

// Destroying the manager mid-stream must not trip an assertion or leak a suspended coroutine.
TEST_F(ResponseFilterManagerTest, DestroyedMidStream) {
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<CountingSseFilter>());
  makeManager(std::move(filters));

  feed("data: {\"a\":1}\n\n", /*end_stream=*/false);
  manager_.reset();
  drain();
}

TEST_F(ResponseFilterManagerTest, CancelIsIdempotent) {
  makeManager({});
  feed("data: {\"a\":1}\n\n", /*end_stream=*/false);

  manager_->cancel();
  manager_->cancel();
  drain();
}

// Suspends on a gate queue before forwarding the first frame, simulating a filter backed up on
// async work.
class SuspendingSseFilter : public AiFilter {
public:
  explicit SuspendingSseFilter(std::shared_ptr<Coroutine::AsyncQueue<bool>> gate)
      : gate_(std::move(gate)) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive,
                                          SseStreamPropagator propagate) override {
    bool first = true;
    while (true) {
      ASSIGN_OR_CO_RETURN(auto event, co_await receive());
      if (!event.has_value()) {
        co_return absl::OkStatus();
      }
      if (first) {
        first = false;
        ASSIGN_OR_CO_RETURN(auto unblock, co_await gate_->pop());
        (void)unblock;
      }
      CO_RETURN_IF_ERROR(co_await propagate(std::move(*event)));
    }
  }

private:
  std::shared_ptr<Coroutine::AsyncQueue<bool>> gate_;
};

// When the pipeline is backed up (e.g. a filter is suspended on async work), tryPush() fails and
// items buffered in pending_items_ charge their byteSize() to the bridge, pausing the filter chain
// once high_watermark_ is crossed and resuming once drainPendingItems() uncharges them.
TEST_F(ResponseFilterManagerTest, SourceIsPausedWhenPipelineIsBackedUp) {
  auto gate = std::make_shared<Coroutine::AsyncQueue<bool>>(/*max_size=*/1);
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<SuspendingSseFilter>(gate));
  makeManager(std::move(filters), ResponseFilterManager::Config{}, /*buffer_limit=*/64);

  // Feed 3 frames at once:
  // - Frame 1 is handed off to SuspendingSseFilter, which suspends on `gate`.
  // - Frame 2 fills stage(0)'s single-slot queue.
  // - Frame 3 fails tryPush(), enters pending_items_, and charges bridge_.addUnacked(byteSize())
  //   which exceeds buffer_limit (64 bytes) and pauses the source.
  Buffer::OwnedImpl buf("data: 1\n\ndata: 2\n\ndata: 3\n\n");
  manager_->onData(buf, /*end_stream=*/true);
  EXPECT_EQ(bridge_->pause_source_calls_, 1);
  EXPECT_EQ(bridge_->resume_source_calls_, 0);

  // Unblock the filter; stage(0) drains pending_items_, uncharging bridge_ and resuming the source.
  gate->tryPush(true);
  drain();

  EXPECT_EQ(bridge_->resume_source_calls_, 1);
  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(output(), "data: 1\n\ndata: 2\n\ndata: 3\n\n");
}

TEST_F(ResponseFilterManagerTest, SourceAccumulatesAndDrainsWhilePipelinePaused) {
  auto gate = std::make_shared<Coroutine::AsyncQueue<bool>>(/*max_size=*/1);
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<SuspendingSseFilter>(gate));
  makeManager(std::move(filters), ResponseFilterManager::Config{}, /*buffer_limit=*/64);

  Buffer::OwnedImpl buf1("data: 1\n\ndata: 2\n\ndata: 3\n\n");
  manager_->onData(buf1, /*end_stream=*/false);
  EXPECT_EQ(bridge_->pause_source_calls_, 1);
  EXPECT_EQ(bridge_->resume_source_calls_, 0);

  // Another chunk arrives before the socket pause takes effect.
  Buffer::OwnedImpl buf2("data: 4\n\n");
  manager_->onData(buf2, /*end_stream=*/true);
  EXPECT_EQ(bridge_->pause_source_calls_, 1);
  EXPECT_EQ(bridge_->resume_source_calls_, 0);

  gate->tryPush(true);
  drain();

  EXPECT_EQ(bridge_->resume_source_calls_, 1);
  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(output(), "data: 1\n\ndata: 2\n\ndata: 3\n\ndata: 4\n\n");
}

// An oversized frame whose payload spills to a per-frame external buffer store still allows a
// filter to mutate inline JSON fields while the large value round-trips by reference.
TEST_F(ResponseFilterManagerTest, SseOversizedFrameSpillsAndRoundTripsThroughFilter) {
  ResponseFilterManager::Config config;
  config.sse.max_in_memory_frame_bytes = 32;
  config.sse.parser.inline_string_threshold_bytes = 16;

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TaggingSseFilter>("tagged"));
  makeManager(std::move(filters), config);

  const std::string big(128, 'z');
  const std::string frame = absl::StrCat("data: {\"big\":\"", big, "\"}\n\n");
  feed(frame);

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(complete_calls_, 1);
  const nlohmann::json parsed = nlohmann::json::parse(output().substr(6, output().size() - 8));
  EXPECT_EQ(parsed["big"], big);
  EXPECT_TRUE(parsed["tagged"]);
}

// A cancelled pipeline has nowhere to put further bytes; accepting them would resurrect a torn
// down chain.
TEST_F(ResponseFilterManagerTest, DataAfterCancelIsDropped) {
  makeManager({});
  manager_->cancel();

  feed("data: {\"a\":1}\n\n");
  EXPECT_EQ(complete_calls_, 0);
  EXPECT_EQ(output(), "");
}

// Synchronous destruction of ResponseFilterManager inside the on_complete callback must not UAF
// the unwinding AsyncState on the call stack.
TEST_F(ResponseFilterManagerTest, DestroyedInsideOnCompleteCallbackIsSafe) {
  bool callback_ran = false;
  bridge_ = std::make_unique<FakeBridge>(*dispatcher_, 1024 * 1024);
  out_buffer_manager_ =
      std::make_unique<BufferManager>(BufferManager::Config{}, factory_, *bridge_);
  manager_ = std::make_unique<ResponseFilterManager>(
      std::vector<AiFilterSharedPtr>{}, factory_, *bridge_, *out_buffer_manager_,
      [&](absl::Status status) {
        callback_ran = true;
        result_ = std::move(status);
        manager_.reset();
      },
      ResponseFilterManager::Config{});

  Buffer::OwnedImpl buf("data: {\"ok\":1}\n\n");
  manager_->onData(buf, true);
  drain();

  EXPECT_TRUE(callback_ran);
  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(manager_, nullptr);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
