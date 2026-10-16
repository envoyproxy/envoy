#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"
#include "source/extensions/filters/http/ai_protocol_manager/upstream_candidates.h"

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
        bridge_(*dispatcher_), buffer_manager_(BufferManager::Config{}, factory_, bridge_),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  void drain() {
    for (int i = 0; i < 20; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  // Declared before buffer_manager_ so it outlives the manager that references it.
  FakeBridge bridge_;
  BufferManager buffer_manager_;
  StreamInfo::StreamInfoImpl stream_info_;
};

// 0-filter pass-through
TEST_F(FilterManagerTest, ZeroFilterPassThrough) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterSharedPtr> filters;
  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;

  manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&status, &completed](absl::Status s) {
                         status = std::move(s);
                         completed = true;
                       });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_.injected_.toString());
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
    req->json()["model"] = target_model_;
    co_return co_await std::move(propagate_request)(std::move(req));
  }

private:
  std::string target_model_;
};

TEST_F(FilterManagerTest, SingleFilterMutation) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-3.5"}});

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestMutationFilter>("gpt-4-turbo"));

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&status, &completed](absl::Status s) {
                         status = std::move(s);
                         completed = true;
                       });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_.injected_.toString());
  EXPECT_EQ(parsed["model"], "gpt-4-turbo");
}

class TestFieldAdderFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    req->json()["temperature"] = 0.5;
    co_return co_await std::move(propagate_request)(std::move(req));
  }
};

class TestFieldModifierFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    EXPECT_DOUBLE_EQ(req->json()["temperature"].get<double>(), 0.5);
    req->json()["temperature"] = 0.9;
    co_return co_await std::move(propagate_request)(std::move(req));
  }
};

TEST_F(FilterManagerTest, MultiFilterPipeline) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestFieldAdderFilter>());
  filters.push_back(std::make_unique<TestFieldModifierFilter>());

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&status, &completed](absl::Status s) {
                         status = std::move(s);
                         completed = true;
                       });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_.injected_.toString());
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

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestErrorFilter>());

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&status, &completed](absl::Status s) {
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

  std::vector<AiFilterSharedPtr> filters;
  // Filter 0 bypasses itself
  filters.push_back(std::make_unique<TestBypassFilter>());
  // Filter 1 still receives and modifies the request
  filters.push_back(std::make_unique<TestMutationFilter>("gpt-4-turbo"));

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&status, &completed](absl::Status s) {
                         status = std::move(s);
                         completed = true;
                       });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_.injected_.toString());
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

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestDropFilter>());

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&status, &completed](absl::Status s) {
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

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestLocalReplyFilter>());

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      /*request_headers=*/nullptr,
      [&local_reply_code, &local_reply_details](Http::Code code, std::string body,
                                                absl::string_view details) {
        local_reply_code = code;
        local_reply_details = std::move(body);
        EXPECT_EQ(details, "ai_protocol_manager_filter_rejected");
      });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kCancelled));
  EXPECT_EQ(local_reply_code, Http::Code::Unauthorized);
  EXPECT_EQ(local_reply_details, "access denied by auth filter");
}

TEST_F(FilterManagerTest, FilterErrorTriggersLocalReply) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  Http::Code local_reply_code = Http::Code::OK;
  std::string local_reply_details;

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestErrorFilter>());

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      /*request_headers=*/nullptr,
      [&local_reply_code, &local_reply_details](Http::Code code, std::string body,
                                                absl::string_view details) {
        local_reply_code = code;
        local_reply_details = std::move(body);
        EXPECT_EQ(details, "ai_protocol_manager_filter_rejected");
      });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kInternal));
  EXPECT_EQ(local_reply_code, Http::Code::BadGateway);
  EXPECT_EQ(local_reply_details, "intentional filter error");
}

TEST_F(FilterManagerTest, FilterLocalReplyWithoutLocalReplyFnInvokesCompletionWithCancelled) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestLocalReplyFilter>());

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      /*request_headers=*/nullptr, /*local_reply_fn=*/nullptr);

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kCancelled));
}

class TestImmediateErrorFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::InternalError("synchronous filter startup error");
  }
};

class TestCountingFilter : public AiFilter {
public:
  explicit TestCountingFilter(int& start_count) : start_count_(start_count) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ++start_count_;
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    co_return co_await std::move(propagate_request)(std::move(req));
  }

private:
  int& start_count_;
};

class TestImmediateLocalReplyFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier reply_locally) override {
    std::move(reply_locally)(Http::Code::Unauthorized, "synchronous auth rejection");
    co_return absl::OkStatus();
  }
};

TEST_F(FilterManagerTest, SynchronousFilterErrorStopsSubsequentFilterLaunches) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  int filter2_started = 0;
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestImmediateErrorFilter>());
  filters.push_back(std::make_unique<TestCountingFilter>(filter2_started));

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&status, &completed](absl::Status s) {
                         status = std::move(s);
                         completed = true;
                       });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kInternal));
  EXPECT_EQ(filter2_started, 0);
}

TEST_F(FilterManagerTest, SynchronousFilterLocalReplyStopsSubsequentFilterLaunches) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  Http::Code local_reply_code = Http::Code::OK;
  std::string local_reply_details;
  int filter2_started = 0;

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestImmediateLocalReplyFilter>());
  filters.push_back(std::make_unique<TestCountingFilter>(filter2_started));

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      /*request_headers=*/nullptr,
      [&local_reply_code, &local_reply_details](Http::Code code, std::string body,
                                                absl::string_view details) {
        local_reply_code = code;
        local_reply_details = std::move(body);
        EXPECT_EQ(details, "ai_protocol_manager_filter_rejected");
      });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_EQ(local_reply_code, Http::Code::Unauthorized);
  EXPECT_EQ(local_reply_details, "synchronous auth rejection");
  EXPECT_EQ(filter2_started, 0);
}

TEST_F(FilterManagerTest, CancelCancelsCoroutines) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestFieldAdderFilter>());

  auto manager = std::make_unique<FilterManager>(std::move(filters));
  manager->startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                        [](absl::Status) {});

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
      auto status =
          co_await std::move(propagate_request)(std::make_unique<AiRequest>(JsonWithExtBuf()));
      EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kCancelled));

      // Calling reply_locally should safely no-op without crashing.
      std::move(reply_locally)(Http::Code::Unauthorized, "too late");

      co_return absl::OkStatus();
    }

  private:
    std::shared_ptr<Coroutine::AsyncQueue<bool>> queue_;
  };

  auto queue = std::make_shared<Coroutine::AsyncQueue<bool>>(1);
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<SuspendingFilter>(queue));

  auto manager = std::make_unique<FilterManager>(std::move(filters));

  bool completed = false;
  manager->startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                        [&completed](absl::Status) { completed = true; });
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
          auto status2 = co_await std::move(p2)(std::make_unique<AiRequest>(JsonWithExtBuf()));
          EXPECT_TRUE(status2.ok());
          EXPECT_FALSE(p2.valid());
          std::ignore = co_await std::move(p2)(std::make_unique<AiRequest>(JsonWithExtBuf()));
          co_return absl::OkStatus();
        };
        auto handle =
            Coroutine::launch(test_coro(), executor, [](auto) {}, Coroutine::StartMode::Inline);
      },
      "AiRequestPropagator invoked on an invalid or already moved instance");
}

class TestNullPropagatorFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    co_return co_await std::move(propagate_request)(nullptr);
  }
};

TEST_F(FilterManagerTest, FilterNullPropagationFails) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestNullPropagatorFilter>());

  FilterManager manager(std::move(filters));

  EXPECT_ENVOY_BUG(
      {
        absl::Status status;
        bool completed = false;
        manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                             [&status, &completed](absl::Status s) {
                               status = std::move(s);
                               completed = true;
                             });
        drain();
        EXPECT_TRUE(completed);
        EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kInvalidArgument));
      },
      "cannot propagate null AiRequestPtr");
}

// Rewrites the request path, as a transcoder does.
class TestPathRewriteFilter : public AiFilter {
public:
  explicit TestPathRewriteFilter(Http::RequestHeaderMap& headers) : headers_(headers) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    headers_.setPath("/v1/messages");
    co_return co_await std::move(propagate_request)(std::move(req));
  }

private:
  Http::RequestHeaderMap& headers_;
};

TEST_F(FilterManagerTest, OnFiltersDoneRunsBeforeTheBodyIsWritten) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/v1/chat/completions"}};

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestPathRewriteFilter>(headers));
  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  bool filters_done = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      &headers, /*local_reply_fn=*/nullptr, /*always_serialize=*/true, LLMProtocol::Unspecified,
      [this, &filters_done, &headers,
       &completed](AiRequest& request,
                   std::shared_ptr<UpstreamCandidates> decision) -> std::optional<SinkReply> {
        filters_done = true;
        EXPECT_EQ(request.json()["model"], "gpt-4");
        EXPECT_EQ(decision, nullptr);
        EXPECT_EQ(headers.getPathValue(), "/v1/messages");
        EXPECT_EQ(bridge_.injected_.length(), 0);
        EXPECT_FALSE(completed);
        return std::nullopt;
      });

  drain();
  EXPECT_TRUE(filters_done);
  EXPECT_TRUE(completed);
  ASSERT_OK(status);
  EXPECT_GT(bridge_.injected_.length(), 0);
}

TEST_F(FilterManagerTest, OnFiltersDoneSkippedOnLocalReply) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestLocalReplyFilter>());
  FilterManager manager(std::move(filters));

  bool completed = false;
  bool filters_done = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&completed](absl::Status) { completed = true; }, /*request_headers=*/nullptr,
      [](Http::Code, std::string, absl::string_view) {}, /*always_serialize=*/true,
      LLMProtocol::Unspecified,
      [&filters_done](AiRequest&, std::shared_ptr<UpstreamCandidates>) -> std::optional<SinkReply> {
        filters_done = true;
        return std::nullopt;
      });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_FALSE(filters_done);
}

// Propagates a new request in place of the one it received.
class TestReplacingFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    JsonWithExtBuf doc;
    doc.setJson(req->json());
    co_return co_await std::move(propagate_request)(std::make_unique<AiRequest>(std::move(doc)));
  }
};

// Runs a test's edits with its callbacks before receiving, in its window, and after propagating.
class EditingFilter : public AiFilter {
public:
  using EditFn = std::function<void(AiFilterCallbacks&)>;

  explicit EditingFilter(EditFn in_window, EditFn before_receive = nullptr,
                         EditFn after_propagate = nullptr)
      : in_window_(std::move(in_window)), before_receive_(std::move(before_receive)),
        after_propagate_(std::move(after_propagate)) {}

  void setAiFilterCallbacks(std::shared_ptr<AiFilterCallbacks> callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    if (before_receive_ != nullptr) {
      before_receive_(*callbacks_);
    }
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    if (in_window_ != nullptr) {
      in_window_(*callbacks_);
    }
    CO_RETURN_IF_ERROR(co_await std::move(propagate_request)(std::move(req)));
    if (after_propagate_ != nullptr) {
      after_propagate_(*callbacks_);
    }
    co_return absl::OkStatus();
  }

  std::shared_ptr<AiFilterCallbacks> callbacks_;

private:
  const EditFn in_window_;
  const EditFn before_receive_;
  const EditFn after_propagate_;
};

// Bypasses itself, keeping its callbacks.
class CallbacksKeepingBypassFilter : public AiFilter {
public:
  void setAiFilterCallbacks(std::shared_ptr<AiFilterCallbacks> callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  Coroutine::Task<absl::Status> decode(AiRequestReceiver, AiRequestPropagator,
                                       LocalReplier) override {
    co_return absl::OkStatus();
  }

  std::shared_ptr<AiFilterCallbacks> callbacks_;
};

std::vector<UpstreamCandidate> candidates() {
  return {{"a", "cluster_a", "model-a", "/a"}, {"b", "cluster_b", "", ""}};
}

// Receives the request and holds it until `resume` is pushed, keeping its callbacks.
class HoldingFilter : public AiFilter {
public:
  explicit HoldingFilter(std::shared_ptr<Coroutine::AsyncQueue<bool>> resume)
      : resume_(std::move(resume)) {}

  void setAiFilterCallbacks(std::shared_ptr<AiFilterCallbacks> callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    std::ignore = co_await resume_->pop();
    co_return co_await std::move(propagate_request)(std::move(req));
  }

  std::shared_ptr<AiFilterCallbacks> callbacks_;

private:
  const std::shared_ptr<Coroutine::AsyncQueue<bool>> resume_;
};

// Seeds in its window, replies locally, then runs a test's edits with its callbacks.
class SeedThenReplyFilter : public AiFilter {
public:
  explicit SeedThenReplyFilter(EditingFilter::EditFn after_reply)
      : after_reply_(std::move(after_reply)) {}

  void setAiFilterCallbacks(std::shared_ptr<AiFilterCallbacks> callbacks) override {
    callbacks_ = std::move(callbacks);
  }

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request, AiRequestPropagator,
                                       LocalReplier reply_locally) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    EXPECT_OK(callbacks_->upstreamCandidates()->seed(candidates(), ""));
    std::move(reply_locally)(Http::Code::Forbidden, "denied");
    after_reply_(*callbacks_);
    co_return absl::OkStatus();
  }

private:
  std::shared_ptr<AiFilterCallbacks> callbacks_;
  const EditingFilter::EditFn after_reply_;
};

class UpstreamCandidatesEditorTest : public FilterManagerTest {
public:
  void run(std::vector<AiFilterSharedPtr> filters, std::vector<AiFilterStage> stages,
           bool upstream_candidates_enabled = true) {
    JsonWithExtBuf doc;
    doc.setJson(nlohmann::json{{"model", "gpt-4"}});
    manager_ = std::make_unique<FilterManager>(std::move(filters));
    manager_->startRequest(
        std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
        [this](absl::Status status) { status_ = std::move(status); }, &headers_,
        [this](Http::Code code, std::string body, absl::string_view details) {
          reply_code_ = code;
          reply_body_ = std::move(body);
          reply_details_ = std::string(details);
        },
        /*always_serialize=*/true, LLMProtocol::OpenAiChatCompletions,
        [this](AiRequest&, std::shared_ptr<UpstreamCandidates> decision) {
          ++hook_calls_;
          decision_ = std::move(decision);
          if (on_hook_ != nullptr) {
            on_hook_();
          }
          return sink_reply_;
        },
        std::move(stages), upstream_candidates_enabled);
    drain();
  }

  static AiFilterStage seeder(std::string name) { return {std::move(name), {/*seed=*/true}}; }

  Http::TestRequestHeaderMapImpl headers_{{":method", "POST"}, {":path", "/v1/chat/completions"}};
  std::unique_ptr<FilterManager> manager_;
  absl::Status status_ = absl::UnknownError("request did not complete");
  int hook_calls_{0};
  std::shared_ptr<UpstreamCandidates> decision_;
  std::function<void()> on_hook_;
  std::optional<SinkReply> sink_reply_;
  std::optional<Http::Code> reply_code_;
  std::string reply_body_;
  std::string reply_details_;
};

TEST_F(UpstreamCandidatesEditorTest, HookGetsTheSealedSeed) {
  std::shared_ptr<EditingFilter> filter =
      std::make_shared<EditingFilter>([](AiFilterCallbacks& callbacks) {
        EXPECT_EQ(callbacks.filterName(), "f0");
        ASSERT_TRUE(callbacks.upstreamCandidates().has_value());
        EXPECT_EQ(callbacks.upstreamCandidates()->current(), nullptr);
        ASSERT_OK(callbacks.upstreamCandidates()->seed(candidates(), "rule"));
        ASSERT_NE(callbacks.upstreamCandidates()->current(), nullptr);
      });
  on_hook_ = [&filter]() {
    OptRef<UpstreamCandidatesEditor> editor = filter->callbacks_->upstreamCandidates();
    EXPECT_EQ(editor->current(), nullptr);
    EXPECT_THAT(editor->seed(candidates(), ""),
                HasStatusCode(absl::StatusCode::kFailedPrecondition));
  };
  run({filter}, {seeder("f0")});

  ASSERT_OK(status_);
  EXPECT_EQ(hook_calls_, 1);
  ASSERT_NE(decision_, nullptr);
  EXPECT_EQ(decision_->outcome(), UpstreamCandidates::Outcome::Pending);
  ASSERT_EQ(decision_->entries().size(), 2);
  EXPECT_EQ(decision_->entries()[0].candidate.name, "a");
  EXPECT_EQ(decision_->entries()[0].candidate.model, "model-a");
  EXPECT_EQ(decision_->entries()[0].added_by, "f0/rule");
  EXPECT_EQ(decision_->entries()[1].added_by, "f0/rule");
  EXPECT_GT(bridge_.injected_.length(), 0);
}

TEST_F(UpstreamCandidatesEditorTest, HookGetsNullWithoutASeed) {
  run({std::make_shared<EditingFilter>(nullptr)}, {seeder("f0")});

  ASSERT_OK(status_);
  EXPECT_EQ(hook_calls_, 1);
  EXPECT_EQ(decision_, nullptr);
}

TEST_F(UpstreamCandidatesEditorTest, SeedOutsideTheWindowFails) {
  const auto expect_closed = [](AiFilterCallbacks& callbacks) {
    EXPECT_EQ(callbacks.upstreamCandidates()->current(), nullptr);
    EXPECT_THAT(callbacks.upstreamCandidates()->seed(candidates(), ""),
                HasStatusCode(absl::StatusCode::kFailedPrecondition));
  };
  run({std::make_shared<EditingFilter>(nullptr, expect_closed, expect_closed)}, {seeder("f0")});

  ASSERT_OK(status_);
  EXPECT_EQ(decision_, nullptr);
}

// The next filter holds the request, so the sink has not sealed the decision when this filter's
// propagate returns; only its own propagate closes its window.
TEST_F(UpstreamCandidatesEditorTest, PropagateClosesTheWindowBeforeTheSeal) {
  auto resume = std::make_shared<Coroutine::AsyncQueue<bool>>(1);
  auto holder = std::make_shared<HoldingFilter>(resume);
  bool edited = false;
  auto first = std::make_shared<EditingFilter>(nullptr, nullptr, [&](AiFilterCallbacks& callbacks) {
    edited = true;
    ASSERT_OK(holder->callbacks_->upstreamCandidates()->seed(candidates(), ""));
    EXPECT_EQ(callbacks.upstreamCandidates()->current(), nullptr);
    EXPECT_THAT(callbacks.upstreamCandidates()->seed(candidates(), ""),
                HasStatusCode(absl::StatusCode::kFailedPrecondition));
  });
  run({first, holder}, {seeder("f0"), seeder("f1")});
  EXPECT_TRUE(edited);
  EXPECT_EQ(hook_calls_, 0);

  resume->tryPush(true);
  drain();
  ASSERT_OK(status_);
  EXPECT_EQ(hook_calls_, 1);
  ASSERT_NE(decision_, nullptr);
  EXPECT_EQ(decision_->entries()[0].added_by, "f1");
}

// A local reply closes the window of the filter that sent it, though it received and never
// propagated.
TEST_F(UpstreamCandidatesEditorTest, LocalReplyClosesTheWindow) {
  bool edited = false;
  run({std::make_shared<SeedThenReplyFilter>([&edited](AiFilterCallbacks& callbacks) {
        edited = true;
        EXPECT_EQ(callbacks.upstreamCandidates()->current(), nullptr);
        EXPECT_THAT(callbacks.upstreamCandidates()->seed(candidates(), ""),
                    HasStatusCode(absl::StatusCode::kFailedPrecondition));
      })},
      {seeder("f0")});

  EXPECT_TRUE(edited);
  EXPECT_THAT(status_, HasStatusCode(absl::StatusCode::kCancelled));
  EXPECT_EQ(reply_code_, Http::Code::Forbidden);
  EXPECT_EQ(reply_details_, "ai_protocol_manager_filter_rejected");
  EXPECT_EQ(hook_calls_, 0);
}

TEST_F(UpstreamCandidatesEditorTest, BypassedStageNeverOpensAWindow) {
  auto bypassed = std::make_shared<CallbacksKeepingBypassFilter>();
  run({bypassed, std::make_shared<EditingFilter>([&bypassed](AiFilterCallbacks& callbacks) {
         EXPECT_THAT(bypassed->callbacks_->upstreamCandidates()->seed(candidates(), ""),
                     HasStatusCode(absl::StatusCode::kFailedPrecondition));
         ASSERT_OK(callbacks.upstreamCandidates()->seed(candidates(), ""));
       })},
      {seeder("f0"), seeder("f1")});

  ASSERT_OK(status_);
  ASSERT_NE(decision_, nullptr);
  EXPECT_EQ(decision_->entries()[0].added_by, "f1");
}

TEST_F(UpstreamCandidatesEditorTest, SeedNeedsThePolicy) {
  run({std::make_shared<EditingFilter>([](AiFilterCallbacks& callbacks) {
        EXPECT_THAT(callbacks.upstreamCandidates()->seed(candidates(), ""),
                    HasStatusCode(absl::StatusCode::kPermissionDenied));
      })},
      {AiFilterStage{"f0", {}}});

  ASSERT_OK(status_);
  EXPECT_EQ(decision_, nullptr);
}

TEST_F(UpstreamCandidatesEditorTest, LaterFilterSeesTheSeed) {
  auto first = std::make_shared<EditingFilter>(
      [](AiFilterCallbacks& callbacks) {
        ASSERT_OK(callbacks.upstreamCandidates()->seed(candidates(), ""));
      },
      nullptr,
      [](AiFilterCallbacks& callbacks) {
        EXPECT_EQ(callbacks.upstreamCandidates()->current(), nullptr);
      });
  auto second = std::make_shared<EditingFilter>([](AiFilterCallbacks& callbacks) {
    const UpstreamCandidates* current = callbacks.upstreamCandidates()->current();
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->entries()[0].added_by, "f0");
    EXPECT_THAT(callbacks.upstreamCandidates()->seed({{"c", "cluster_c", "", ""}}, ""),
                HasStatusCode(absl::StatusCode::kAlreadyExists));
  });
  run({first, second}, {seeder("f0"), seeder("f1")});

  ASSERT_OK(status_);
  ASSERT_NE(decision_, nullptr);
  ASSERT_EQ(decision_->entries().size(), 2);
  EXPECT_EQ(decision_->entries()[0].candidate.name, "a");
}

TEST_F(UpstreamCandidatesEditorTest, RejectsInvalidCandidates) {
  run({std::make_shared<EditingFilter>([](AiFilterCallbacks& callbacks) {
        UpstreamCandidatesEditor& editor = *callbacks.upstreamCandidates();
        for (std::vector<UpstreamCandidate> invalid : std::vector<std::vector<UpstreamCandidate>>{
                 {},
                 {{"", "cluster_a", "", ""}},
                 {{"a b", "cluster_a", "", ""}},
                 {{std::string(33, 'a'), "cluster_a", "", ""}},
                 {{"a", "cluster_a", "", ""}, {"a", "cluster_b", "", ""}},
                 {{"a", "", "", ""}},
                 {{"a", "cluster_a", "", "relative"}},
                 {{"a", "cluster_a", "", "/a\r\nx-injected: 1"}},
             }) {
          EXPECT_THAT(editor.seed(std::move(invalid), ""),
                      HasStatusCode(absl::StatusCode::kInvalidArgument));
        }
        EXPECT_EQ(editor.current(), nullptr);
        ASSERT_OK(editor.seed(
            {{std::string(32, 'a'), "cluster_a", "", ""}, {"A-Z_a.z-09", "cluster_b", "", ""}},
            ""));
      })},
      {seeder("f0")});

  ASSERT_OK(status_);
  ASSERT_NE(decision_, nullptr);
  EXPECT_EQ(decision_->entries().size(), 2);
}

// The decision is held per stream, so a filter that propagates a new AiRequest keeps it.
TEST_F(UpstreamCandidatesEditorTest, ReplacingTheRequestKeepsTheDecision) {
  run({std::make_shared<EditingFilter>([](AiFilterCallbacks& callbacks) {
         ASSERT_OK(callbacks.upstreamCandidates()->seed(candidates(), ""));
       }),
       std::make_shared<TestReplacingFilter>()},
      {seeder("f0"), AiFilterStage{"f1", {}}});

  ASSERT_OK(status_);
  ASSERT_NE(decision_, nullptr);
  EXPECT_EQ(decision_->entries().size(), 2);
}

TEST_F(UpstreamCandidatesEditorTest, HookReplyEndsTheRequest) {
  sink_reply_ = SinkReply{Http::Code::ServiceUnavailable, "no eligible upstream", "test_details"};
  run({std::make_shared<EditingFilter>(nullptr)}, {seeder("f0")});

  EXPECT_THAT(status_, HasStatusCode(absl::StatusCode::kCancelled));
  EXPECT_EQ(reply_code_, Http::Code::ServiceUnavailable);
  EXPECT_EQ(reply_body_, "no eligible upstream");
  EXPECT_EQ(reply_details_, "test_details");
  EXPECT_EQ(bridge_.injected_.length(), 0);
  EXPECT_EQ(stream_info_.filterState()->getDataReadOnly<APMRequestPayloadIndex>(
                APMRequestPayloadIndex::kFilterStateKey),
            nullptr);
}

TEST_F(UpstreamCandidatesEditorTest, DisabledEditorIsEmpty) {
  run({std::make_shared<EditingFilter>([](AiFilterCallbacks& callbacks) {
        EXPECT_EQ(callbacks.filterName(), "f0");
        EXPECT_FALSE(callbacks.upstreamCandidates().has_value());
      })},
      {seeder("f0")}, /*upstream_candidates_enabled=*/false);

  ASSERT_OK(status_);
  EXPECT_EQ(decision_, nullptr);
}

TEST_F(UpstreamCandidatesEditorTest, StagesDefaultToUnnamed) {
  run({std::make_shared<EditingFilter>([](AiFilterCallbacks& callbacks) {
        EXPECT_EQ(callbacks.filterName(), "");
        EXPECT_THAT(callbacks.upstreamCandidates()->seed(candidates(), ""),
                    HasStatusCode(absl::StatusCode::kPermissionDenied));
      })},
      {});

  ASSERT_OK(status_);
}

TEST_F(UpstreamCandidatesEditorTest, SeedAfterTheChainIsGoneFails) {
  auto filter = std::make_shared<CallbacksKeepingBypassFilter>();
  run({filter}, {seeder("f0")});
  manager_.reset();

  EXPECT_EQ(filter->callbacks_->upstreamCandidates()->current(), nullptr);
  EXPECT_THAT(filter->callbacks_->upstreamCandidates()->seed(candidates(), ""),
              HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST_F(FilterManagerTest, SetsContentLengthOnRequestHeaders) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/chat/completions"}, {"content-length", "1000"}};

  std::vector<AiFilterSharedPtr> filters;
  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      &headers);

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  std::string output = bridge_.injected_.toString();
  EXPECT_EQ(headers.getContentLengthValue(), absl::StrCat(output.size()));
}

TEST_F(FilterManagerTest, SetsContentLengthOnRequestHeadersAfterMutation) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-3.5"}});

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/chat/completions"}, {"content-length", "10"}};

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestMutationFilter>("gpt-4-turbo-extra-long"));

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      &headers);

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  std::string output = bridge_.injected_.toString();
  EXPECT_EQ(headers.getContentLengthValue(), absl::StrCat(output.size()));
}

// Receives the request and passes it on without editing it.
class ReadOnlyFilter : public AiFilter {
public:
  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    EXPECT_EQ(req->json()["model"], "gpt-4");
    co_return co_await std::move(propagate_request)(std::move(req));
  }
};

class FilterManagerReserializationTest : public FilterManagerTest {
public:
  // Key order, spacing and number formatting that re-serialization does not reproduce.
  static constexpr absl::string_view kReceived =
      "{ \"model\" : \"gpt-4\",\n  \"z\": 1, \"a\": 2.50 }";

  // Buffers the body as the AI Protocol Manager does before parsing it.
  JsonWithExtBuf receive() {
    Buffer::OwnedImpl data(kReceived);
    buffer_manager_.onData(data);
    buffer_manager_.endStream();
    JsonWithExtBuf doc;
    doc.setJson(nlohmann::json::parse(kReceived));
    return doc;
  }

  absl::Status run(AiFilterSharedPtr filter, bool always_serialize) {
    std::vector<AiFilterSharedPtr> filters;
    filters.push_back(std::move(filter));
    FilterManager manager(std::move(filters));
    absl::Status status = absl::UnknownError("request did not complete");
    manager.startRequest(
        receive(), &buffer_manager_, *dispatcher_, stream_info_,
        [&status](absl::Status s) { status = std::move(s); }, &headers_,
        /*local_reply_fn=*/nullptr, always_serialize);
    drain();
    return status;
  }

  Http::TestRequestHeaderMapImpl headers_{{":method", "POST"},
                                          {":path", "/chat/completions"},
                                          {"content-length", absl::StrCat(kReceived.size())}};
};

TEST_F(FilterManagerReserializationTest, SerializesByDefault) {
  ASSERT_OK(run(std::make_unique<ReadOnlyFilter>(), /*always_serialize=*/true));

  const std::string output = bridge_.injected_.toString();
  EXPECT_NE(output, kReceived);
  EXPECT_EQ(nlohmann::json::parse(output), nlohmann::json::parse(kReceived));
  EXPECT_EQ(headers_.getContentLengthValue(), absl::StrCat(output.size()));
}

TEST_F(FilterManagerReserializationTest, DisabledForwardsTheReceivedBody) {
  ASSERT_OK(run(std::make_unique<ReadOnlyFilter>(), /*always_serialize=*/false));

  EXPECT_EQ(bridge_.injected_.toString(), kReceived);
  EXPECT_EQ(headers_.getContentLengthValue(), absl::StrCat(kReceived.size()));
  auto* fs = stream_info_.filterState()->getDataReadOnly<APMRequestPayloadIndex>(
      APMRequestPayloadIndex::kFilterStateKey);
  ASSERT_NE(fs, nullptr);
  EXPECT_EQ(fs->index().json()["model"], "gpt-4");
}

TEST_F(FilterManagerReserializationTest, DisabledDoesNotSendFilterEdits) {
  ASSERT_OK(run(std::make_unique<TestMutationFilter>("gpt-4-turbo"), /*always_serialize=*/false));

  EXPECT_EQ(bridge_.injected_.toString(), kReceived);
}

TEST_F(FilterManagerTest, DoesNotSetContentLengthWhenNotPreviouslyPresent) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/chat/completions"}};

  std::vector<AiFilterSharedPtr> filters;
  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      &headers);

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  EXPECT_EQ(headers.ContentLength(), nullptr);
}

// Suspends after propagating, so its coroutine resumes only after the manager is gone.
class OutlivingFilter : public AiFilter {
public:
  OutlivingFilter(std::shared_ptr<Coroutine::AsyncQueue<int>> queue, int& seen, bool& destroyed)
      : queue_(std::move(queue)), seen_(seen), destroyed_(destroyed) {}
  ~OutlivingFilter() override { destroyed_ = true; }

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    CO_RETURN_IF_ERROR(co_await std::move(propagate_request)(std::move(req)));
    ASSIGN_OR_CO_RETURN(std::optional<int> value, co_await queue_->pop());
    seen_ = value.value_or(-1);
    co_return absl::OkStatus();
  }

private:
  std::shared_ptr<Coroutine::AsyncQueue<int>> queue_;
  int& seen_;
  bool& destroyed_;
};

TEST_F(FilterManagerTest, FilterOutlivesManagerUntilItsCoroutineCompletes) {
  auto queue = std::make_shared<Coroutine::AsyncQueue<int>>(/*max_size=*/1);
  int seen = 0;
  bool destroyed = false;

  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_shared<OutlivingFilter>(queue, seen, destroyed));
  auto manager = std::make_unique<FilterManager>(std::move(filters));
  bool completed = false;
  manager->startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                        [&completed](absl::Status s) { completed = s.ok(); });
  drain();
  ASSERT_TRUE(completed);

  manager.reset();
  EXPECT_FALSE(destroyed);

  ASSERT_TRUE(queue->tryPush(7));
  drain();
  EXPECT_EQ(seen, 7);
  EXPECT_TRUE(destroyed);
}

class BidirectionalTracingFilter : public AiFilter {
public:
  BidirectionalTracingFilter(std::string name, std::vector<std::string>& trace)
      : name_(std::move(name)), trace_(trace) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr req, co_await std::move(receive_request)());
    trace_.push_back("decode:" + name_);
    seen_request_model_ = req->json()["model"].get<std::string>();
    co_return co_await std::move(propagate_request)(std::move(req));
  }

  Coroutine::Task<absl::Status> encodeSSE(SseStreamReceiver receive,
                                          SseStreamPropagator propagate) override {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto event, co_await receive());
      if (!event.has_value()) {
        co_return absl::OkStatus();
      }
      trace_.push_back("encode:" + name_);
      if ((*event)->is_json()) {
        (*event)->json().json()[name_ + "_saw_model"] = seen_request_model_;
      }
      CO_RETURN_IF_ERROR(co_await propagate(std::move(*event)));
    }
  }

private:
  std::string name_;
  std::vector<std::string>& trace_;
  std::string seen_request_model_;
};

TEST_F(FilterManagerTest, RequestAndResponseShareFilterInstancesInReverseOrder) {
  std::vector<std::string> trace;
  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_shared<BidirectionalTracingFilter>("A", trace));
  filters.push_back(std::make_shared<BidirectionalTracingFilter>("B", trace));

  FilterManager manager(std::move(filters));

  JsonWithExtBuf req_doc;
  req_doc.setJson(nlohmann::json{{"model", "gpt-4"}});
  bool req_completed = false;
  manager.startRequest(std::move(req_doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&req_completed](absl::Status s) { req_completed = s.ok(); });
  drain();
  ASSERT_TRUE(req_completed);
  EXPECT_EQ(trace, (std::vector<std::string>{"decode:A", "decode:B"}));

  FakeBridge resp_bridge(*dispatcher_);
  BufferManager resp_out_manager(BufferManager::Config{}, factory_, resp_bridge);
  bool resp_completed = false;
  manager.startSseResponse(factory_, resp_bridge, resp_out_manager,
                           [&resp_completed](absl::Status s) { resp_completed = s.ok(); });

  Buffer::OwnedImpl sse_input;
  sse_input.add("data: {\"ok\":true}\n\n");
  manager.onResponseData(sse_input, /*end_stream=*/true);
  drain();
  ASSERT_TRUE(resp_completed);

  EXPECT_EQ(trace, (std::vector<std::string>{"decode:A", "decode:B", "encode:B", "encode:A"}));
  const std::string resp_output = resp_bridge.injected_.toString();
  const nlohmann::json parsed_resp =
      nlohmann::json::parse(resp_output.substr(6, resp_output.size() - 8));
  EXPECT_EQ(parsed_resp["A_saw_model"], "gpt-4");
  EXPECT_EQ(parsed_resp["B_saw_model"], "gpt-4");

  resp_out_manager.onDestroy();
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
