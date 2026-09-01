#include <memory>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"

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

using ::Envoy::StatusHelpers::HasStatusCode;

class FilterManagerTest : public testing::Test {
public:
  FilterManagerTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")), factory_(),
        bridge_raw_(new FakeBridge(*dispatcher_)),
        buffer_manager_(factory_, std::unique_ptr<FakeBridge>(bridge_raw_)),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  ~FilterManagerTest() override { buffer_manager_.onDestroy(); }

  void drain() {
    for (int i = 0; i < 20; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  FakeBridge* bridge_raw_{nullptr};
  BufferManager buffer_manager_;
  StreamInfo::StreamInfoImpl stream_info_;
};

// 0-filter pass-through
TEST_F(FilterManagerTest, ZeroFilterPassThrough) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterPtr> filters;
  FilterManager manager(std::move(filters), std::move(doc), &buffer_manager_, *dispatcher_,
                        stream_info_);

  absl::Status status;
  bool completed = false;

  manager.start([&status, &completed](absl::Status s) {
    status = std::move(s);
    completed = true;
  });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_raw_->injected_.toString());
  EXPECT_EQ(parsed["model"], "gpt-4");

  auto* fs = stream_info_.filterState()->getDataReadOnly<APMRequestPayloadIndex>(
      APMRequestPayloadIndex::kFilterStateKey);
  ASSERT_NE(fs, nullptr);
  EXPECT_EQ(fs->index().json()["model"], "gpt-4");
}

class TestMutationFilter : public AiFilter {
public:
  explicit TestMutationFilter(std::string target_model) : target_model_(std::move(target_model)) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    req->request_index().json()["model"] = target_model_;
    co_return co_await std::move(propagate_request)(std::move(req));
  }

private:
  std::string target_model_;
};

TEST_F(FilterManagerTest, SingleFilterMutation) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-3.5"}});

  std::vector<AiFilterPtr> filters;
  filters.push_back(std::make_unique<TestMutationFilter>("gpt-4-turbo"));

  FilterManager manager(std::move(filters), std::move(doc), &buffer_manager_, *dispatcher_,
                        stream_info_);

  absl::Status status;
  bool completed = false;
  manager.start([&status, &completed](absl::Status s) {
    status = std::move(s);
    completed = true;
  });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_raw_->injected_.toString());
  EXPECT_EQ(parsed["model"], "gpt-4-turbo");
}

class TestFieldAdderFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    req->request_index().json()["temperature"] = 0.5;
    co_return co_await std::move(propagate_request)(std::move(req));
  }
};

class TestFieldModifierFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    EXPECT_DOUBLE_EQ(req->request_index().json()["temperature"].get<double>(), 0.5);
    req->request_index().json()["temperature"] = 0.9;
    co_return co_await std::move(propagate_request)(std::move(req));
  }
};

TEST_F(FilterManagerTest, MultiFilterPipeline) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterPtr> filters;
  filters.push_back(std::make_unique<TestFieldAdderFilter>());
  filters.push_back(std::make_unique<TestFieldModifierFilter>());

  FilterManager manager(std::move(filters), std::move(doc), &buffer_manager_, *dispatcher_,
                        stream_info_);

  absl::Status status;
  bool completed = false;
  manager.start([&status, &completed](absl::Status s) {
    status = std::move(s);
    completed = true;
  });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_raw_->injected_.toString());
  EXPECT_EQ(parsed["model"], "gpt-4");
  EXPECT_DOUBLE_EQ(parsed["temperature"].get<double>(), 0.9);
}

class TestErrorFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request, AiRequestPropagator,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    co_return absl::InternalError("intentional filter error");
  }
};

TEST_F(FilterManagerTest, FilterErrorPropagation) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterPtr> filters;
  filters.push_back(std::make_unique<TestErrorFilter>());

  FilterManager manager(std::move(filters), std::move(doc), &buffer_manager_, *dispatcher_,
                        stream_info_);

  absl::Status status;
  bool completed = false;
  manager.start([&status, &completed](absl::Status s) {
    status = std::move(s);
    completed = true;
  });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kInternal));
}

class TestBypassFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    // Early returns without calling receive_request or propagate_request or reply_locally
    co_return absl::OkStatus();
  }
};

TEST_F(FilterManagerTest, FilterBypassEarlyReturnPassesThrough) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-3.5"}});

  std::vector<AiFilterPtr> filters;
  // Filter 0 bypasses itself
  filters.push_back(std::make_unique<TestBypassFilter>());
  // Filter 1 still receives and modifies the request
  filters.push_back(std::make_unique<TestMutationFilter>("gpt-4-turbo"));

  FilterManager manager(std::move(filters), std::move(doc), &buffer_manager_, *dispatcher_,
                        stream_info_);

  absl::Status status;
  bool completed = false;
  manager.start([&status, &completed](absl::Status s) {
    status = std::move(s);
    completed = true;
  });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_raw_->injected_.toString());
  EXPECT_EQ(parsed["model"], "gpt-4-turbo");
}

class TestDropFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request, AiRequestPropagator,
                                       LocalReplier) override {
    // Receives request but never propagates or sends local reply
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    co_return absl::OkStatus();
  }
};

TEST_F(FilterManagerTest, FilterConsumedWithoutPropagationFails) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterPtr> filters;
  filters.push_back(std::make_unique<TestDropFilter>());

  FilterManager manager(std::move(filters), std::move(doc), &buffer_manager_, *dispatcher_,
                        stream_info_);

  absl::Status status;
  bool completed = false;
  manager.start([&status, &completed](absl::Status s) {
    status = std::move(s);
    completed = true;
  });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kInternal));
}

class TestLocalReplyFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request, AiRequestPropagator,
                                       LocalReplier reply_locally) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    std::move(reply_locally)(Http::Code::Unauthorized, "access denied by auth filter");
    co_return absl::OkStatus();
  }
};

TEST_F(FilterManagerTest, FilterLocalReply) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  Http::Code local_reply_code = Http::Code::OK;
  std::string local_reply_details;

  std::vector<AiFilterPtr> filters;
  filters.push_back(std::make_unique<TestLocalReplyFilter>());

  FilterManager manager(
      std::move(filters), std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&local_reply_code, &local_reply_details](Http::Code code, std::string details) {
        local_reply_code = code;
        local_reply_details = std::move(details);
      });

  absl::Status status;
  bool completed = false;
  manager.start([&status, &completed](absl::Status s) {
    status = std::move(s);
    completed = true;
  });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kCancelled));
  EXPECT_EQ(local_reply_code, Http::Code::Unauthorized);
  EXPECT_EQ(local_reply_details, "access denied by auth filter");
}

TEST_F(FilterManagerTest, CancelCancelsCoroutines) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterPtr> filters;
  filters.push_back(std::make_unique<TestFieldAdderFilter>());

  auto manager = std::make_unique<FilterManager>(std::move(filters), std::move(doc),
                                                 &buffer_manager_, *dispatcher_, stream_info_);

  manager->cancel();
  manager.reset();
}

TEST_F(FilterManagerTest, DestructWhileSuspendedIsSafe) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  class SuspendingFilter : public AiFilter {
  public:
    explicit SuspendingFilter(std::shared_ptr<Coroutine::AsyncQueue<bool>> queue)
        : queue_(std::move(queue)) {}

    Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                         AiRequestPropagator propagate_request,
                                         LocalReplier reply_locally) override {
      // Wait on an external suspension point (e.g. async gRPC / queue).
      std::ignore = co_await queue_->pop();

      // Manager is destructed during the above suspension point.
      // Calling receive_request should now safely return CancelledError without UAF.
      auto req_or = co_await std::move(receive_request)();
      EXPECT_THAT(req_or.status(), HasStatusCode(absl::StatusCode::kCancelled));

      // Calling propagate_request should also safely return CancelledError.
      auto status = co_await std::move(propagate_request)(nullptr);
      EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kCancelled));

      // Calling reply_locally should safely no-op without crashing.
      std::move(reply_locally)(Http::Code::Unauthorized, "too late");

      co_return absl::OkStatus();
    }

  private:
    std::shared_ptr<Coroutine::AsyncQueue<bool>> queue_;
  };

  auto queue = std::make_shared<Coroutine::AsyncQueue<bool>>(1);
  std::vector<AiFilterPtr> filters;
  filters.push_back(std::make_unique<SuspendingFilter>(queue));

  auto manager = std::make_unique<FilterManager>(std::move(filters), std::move(doc),
                                                 &buffer_manager_, *dispatcher_, stream_info_);

  bool completed = false;
  manager->start([&completed](absl::Status) { completed = true; });
  drain();

  // Destruct the manager while the filter is still suspended in queue_->pop().
  manager.reset();

  // Now resume the suspended filter coroutine.
  queue->tryPush(true);
  drain();
}

TEST_F(FilterManagerTest, InvalidReceiverAndPropagatorInvocation) {
  AiRequestReceiver empty_receiver(nullptr);
  EXPECT_FALSE(empty_receiver.valid());

  AiRequestPropagator empty_propagator(nullptr);
  EXPECT_FALSE(empty_propagator.valid());

  auto executor = std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_);

  EXPECT_ENVOY_BUG(
      {
        auto test_coro = []() -> Coroutine::Task<absl::Status> {
          AiRequestReceiver r(nullptr);
          std::ignore = co_await std::move(r)();
          co_return absl::OkStatus();
        };
        auto handle =
            Coroutine::launch(test_coro(), executor, [](auto) {}, Coroutine::StartMode::Inline);
      },
      "AiRequestReceiver invoked on an invalid or already moved instance");

  EXPECT_ENVOY_BUG(
      {
        auto test_coro = []() -> Coroutine::Task<absl::Status> {
          AiRequestPropagator p(nullptr);
          std::ignore = co_await std::move(p)(nullptr);
          co_return absl::OkStatus();
        };
        auto handle =
            Coroutine::launch(test_coro(), executor, [](auto) {}, Coroutine::StartMode::Inline);
      },
      "AiRequestPropagator invoked on an invalid or already moved instance");

  EXPECT_ENVOY_BUG(
      {
        auto test_coro = []() -> Coroutine::Task<absl::Status> {
          AiRequestReceiver r2([]() -> Coroutine::Task<absl::StatusOr<AiRequestPtr>> {
            co_return std::make_unique<AiRequest>(JsonWithExtBuf());
          });
          EXPECT_TRUE(r2.valid());
          auto res2 = co_await std::move(r2)();
          EXPECT_TRUE(res2.ok());
          EXPECT_FALSE(r2.valid());
          std::ignore = co_await std::move(r2)();
          co_return absl::OkStatus();
        };
        auto handle =
            Coroutine::launch(test_coro(), executor, [](auto) {}, Coroutine::StartMode::Inline);
      },
      "AiRequestReceiver invoked on an invalid or already moved instance");

  EXPECT_ENVOY_BUG(
      {
        auto test_coro = []() -> Coroutine::Task<absl::Status> {
          AiRequestPropagator p2(
              [](AiRequestPtr) -> Coroutine::Task<absl::Status> { co_return absl::OkStatus(); });
          EXPECT_TRUE(p2.valid());
          auto status2 = co_await std::move(p2)(nullptr);
          EXPECT_TRUE(status2.ok());
          EXPECT_FALSE(p2.valid());
          std::ignore = co_await std::move(p2)(nullptr);
          co_return absl::OkStatus();
        };
        auto handle =
            Coroutine::launch(test_coro(), executor, [](auto) {}, Coroutine::StartMode::Inline);
      },
      "AiRequestPropagator invoked on an invalid or already moved instance");
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
