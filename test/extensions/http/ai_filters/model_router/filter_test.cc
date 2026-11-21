#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/data/ai/v3/request_info.pb.h"
#include "envoy/data/ai/v3/token_usage.pb.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"

#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/http/ai_filters/model_router/filter.h"
#include "source/extensions/http/ai_filters/model_router/quota_interface.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {
namespace {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::AiRequestPropagator;
using HttpFilters::AiProtocolManager::AiRequestPtr;
using HttpFilters::AiProtocolManager::AiRequestReceiver;
using HttpFilters::AiProtocolManager::FlattenJsonField;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LlmRoutesFilterStateObject;
using HttpFilters::AiProtocolManager::SseEvent;
using HttpFilters::AiProtocolManager::SseEventPtr;
namespace FilterStateKeys = HttpFilters::AiProtocolManager::FilterStateKeys;
using ModelRouterProto = envoy::extensions::http::ai_filters::model_router::v3::ModelRouter;

class MockQuota : public QuotaInterface {
public:
  static Coroutine::Task<absl::StatusOr<bool>> makeCheckTask(absl::StatusOr<bool> res) {
    co_return res;
  }
  static Coroutine::Task<absl::Status> makeConsumeTask(absl::Status res) { co_return res; }

  MOCK_METHOD(Coroutine::Task<absl::StatusOr<bool>>, checkLimits,
              (std::string provider, std::string model, uint64_t estimated_input_tokens),
              (override));
  MOCK_METHOD(Coroutine::Task<absl::Status>, consumeQuota,
              (std::string provider, std::string model, uint64_t input_tokens,
               uint64_t output_tokens),
              (override));
};

class ModelRouterFilterTest : public testing::Test {
public:
  ModelRouterFilterTest()
      : api_(Api::createApiForTest()),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain),
        mock_quota_(std::make_shared<StrictMock<MockQuota>>()) {}

  template <typename T> static T runTask(Coroutine::Task<T> task) {
    auto exec = std::make_shared<Coroutine::ManualExecutor>();
    std::optional<T> result;
    auto handle = Coroutine::launch(
        std::move(task), exec, [&result](T val) { result.emplace(std::move(val)); },
        Coroutine::StartMode::Inline);
    exec->drain();
    RELEASE_ASSERT(result.has_value(), "Task did not complete synchronously");
    return std::move(*result);
  }

  ModelRouterProto defaultProto() {
    ModelRouterProto proto;
    TestUtility::loadFromYaml(R"EOF(
providers:
  - name: anthropic
    llm_protocol: ANTHROPIC_MESSAGES
    host_and_port: api.anthropic.com
    binding:
      uri_pattern: /v1/messages
  - name: primary_openai
    llm_protocol: OPENAI_CHAT_COMPLETIONS
    host_and_port: primary.llm.example.com:8443
    binding:
      uri_pattern: /v1/chat/completions
models:
  - name: haiku
    provider: anthropic
    model: claude-haiku-4-5
    api_key:
      inline_string: "Bearer sk-ant"
  - name: gpt4o
    provider: primary_openai
    model: gpt-4o
    api_key:
      inline_string: "Bearer sk-oai"
  - name: gpt4o_mini
    provider: primary_openai
    model: gpt-4o-mini
    api_key:
      inline_string: "Bearer sk-mini"
  - name: gpt4o_nano
    provider: primary_openai
    model: gpt-4o-nano
    api_key:
      inline_string: "Bearer sk-nano"
routes:
  - requested_model: send_me_to_anthropic
    targets:
      - model: haiku
  - requested_model: multi_fallback_model
    targets:
      - model: gpt4o
      - model: gpt4o_mini
      - model: gpt4o_nano
)EOF",
                              proto);
    return proto;
  }

  ModelRouterFilterConfigSharedPtr
  defaultConfig(ModelRouterFilterConfig::QuotaPtr tl_quota = nullptr) {
    auto config_or =
        ModelRouterFilterConfig::create(defaultProto(), *api_, *stats_store_.rootScope());
    EXPECT_TRUE(config_or.ok()) << config_or.status();
    (*config_or)->setQuotas(std::move(tl_quota));
    return *std::move(config_or);
  }

  ModelRouterFilterConfig::QuotaPtr makeMockQuotaSlot() {
    auto slot = ThreadLocal::TypedSlot<QuotaInterface>::makeUnique(tls_);
    slot->set([q = mock_quota_](Event::Dispatcher&) { return q; });
    return slot;
  }

  void expectCheckLimits(absl::string_view provider, absl::string_view model,
                         uint64_t estimated_input_tokens, absl::StatusOr<bool> result) {
    EXPECT_CALL(*mock_quota_,
                checkLimits(std::string(provider), std::string(model), estimated_input_tokens))
        .WillOnce([result = std::move(result)](std::string, std::string, uint64_t) {
          return MockQuota::makeCheckTask(result);
        });
  }

  void expectConsumeQuota(absl::string_view provider, absl::string_view model,
                          uint64_t input_tokens, uint64_t output_tokens,
                          absl::Status status = absl::OkStatus()) {
    EXPECT_CALL(*mock_quota_, consumeQuota(std::string(provider), std::string(model), input_tokens,
                                           output_tokens))
        .WillOnce([status = std::move(status)](std::string, std::string, uint64_t, uint64_t) {
          return MockQuota::makeConsumeTask(status);
        });
  }

  void setModelInFilterState(absl::string_view model_name) {
    stream_info_.filterState()->setData(FilterStateKeys::ModelRequest,
                                        std::make_shared<Router::StringAccessorImpl>(model_name),
                                        StreamInfo::FilterState::LifeSpan::FilterChain);
  }

  void setEstimatedInputTokensInMetadata(uint64_t estimated_input_tokens) {
    envoy::data::ai::v3::RequestInfo info;
    info.mutable_estimated_input_tokens()->set_value(estimated_input_tokens);
    Protobuf::Any any;
    MessageUtil::packFrom(any, info);
    stream_info_.setDynamicTypedMetadata("envoy.ai.request_info", any);
  }

  void setTokenUsageInMetadata(std::optional<uint64_t> input_tokens,
                               std::optional<uint64_t> output_tokens) {
    envoy::data::ai::v3::TokenUsage usage;
    if (input_tokens.has_value()) {
      usage.mutable_input_tokens()->set_value(*input_tokens);
    }
    if (output_tokens.has_value()) {
      usage.mutable_output_tokens()->set_value(*output_tokens);
    }
    Protobuf::Any any;
    MessageUtil::packFrom(any, usage);
    stream_info_.setDynamicTypedMetadata("envoy.ai.token_usage", any);
  }

  size_t runResponse(bool sse = false, ModelRouterFilterConfigSharedPtr config = nullptr,
                     std::optional<uint32_t> response_code = 200) {
    if (response_code.has_value()) {
      stream_info_.setResponseCode(*response_code);
    }
    const AiFilterContext context{stream_info_, request_headers_,
                                  LLMProtocol::OpenAiChatCompletions};
    ModelRouterFilter filter(config != nullptr ? std::move(config) : defaultConfig(), context);

    size_t sent = 0;
    size_t propagated = 0;
    absl::Status status;
    if (sse) {
      status = runTask(filter.encodeSSE(
          [&sent]() -> Coroutine::Task<absl::StatusOr<std::optional<SseEventPtr>>> {
            if (sent++ == 0) {
              co_return std::make_unique<SseEvent>();
            }
            co_return std::nullopt;
          },
          [&propagated](SseEventPtr event) -> Coroutine::Task<absl::Status> {
            EXPECT_NE(event, nullptr);
            ++propagated;
            co_return absl::OkStatus();
          }));
    } else {
      status = runTask(filter.encodeUnary(
          [&sent]() -> Coroutine::Task<absl::StatusOr<std::vector<FlattenJsonField>>> {
            if (sent++ == 0) {
              co_return std::vector<FlattenJsonField>(1);
            }
            co_return std::vector<FlattenJsonField>{};
          },
          [&propagated](std::vector<FlattenJsonField> batch) -> Coroutine::Task<absl::Status> {
            EXPECT_FALSE(batch.empty());
            ++propagated;
            co_return absl::OkStatus();
          }));
    }
    EXPECT_TRUE(status.ok()) << status;
    return propagated;
  }

  void run(ModelRouterFilterConfigSharedPtr config = nullptr, bool expect_local_reply = false) {
    reply_code_.reset();
    reply_details_.clear();
    request_propagated_ = false;
    request_headers_ = Http::TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", "/v1/chat/completions"}, {":authority", "original.host"}};

    const AiFilterContext context{stream_info_, request_headers_,
                                  LLMProtocol::OpenAiChatCompletions};
    ModelRouterFilter filter(config != nullptr ? std::move(config) : defaultConfig(), context);

    const absl::Status status = runTask(filter.decode(
        AiRequestReceiver([]() -> Coroutine::Task<absl::StatusOr<AiRequestPtr>> {
          co_return std::make_unique<AiRequest>(JsonWithExtBuf{});
        }),
        AiRequestPropagator([this](AiRequestPtr req) -> Coroutine::Task<absl::Status> {
          EXPECT_NE(req, nullptr);
          request_propagated_ = true;
          co_return absl::OkStatus();
        }),
        [this](Http::Code code, std::string details) {
          reply_code_ = code;
          reply_details_ = std::move(details);
        }));
    EXPECT_TRUE(status.ok()) << status;
    EXPECT_EQ(request_propagated_, !expect_local_reply);
  }

  void expectLocalReply(Http::Code expected_code, absl::string_view expected_details) {
    EXPECT_EQ(reply_code_, expected_code);
    EXPECT_EQ(reply_details_, expected_details);
    EXPECT_FALSE(request_propagated_);
  }

  const LlmRoutesFilterStateObject* resolvedRoutes() {
    return stream_info_.filterState()->getDataReadOnly<LlmRoutesFilterStateObject>(
        FilterStateKeys::ResolvedRoutes);
  }

  uint64_t counterValue(const std::string& name) {
    const auto counter =
        TestUtility::findCounter(stats_store_, "ai_protocol_manager.model_router." + name);
    return counter != nullptr ? counter->value() : 0;
  }

  Api::ApiPtr api_;
  StreamInfo::StreamInfoImpl stream_info_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  std::shared_ptr<StrictMock<MockQuota>> mock_quota_;
  Http::TestRequestHeaderMapImpl request_headers_;
  std::optional<Http::Code> reply_code_;
  std::string reply_details_;
  bool request_propagated_{false};
};

TEST_F(ModelRouterFilterTest, ModelNameMissingInFilterState) {
  run(/*config=*/nullptr, /*expect_local_reply=*/true);
  expectLocalReply(Http::Code::NotFound, "Model name not found in filter state");

  EXPECT_EQ(resolvedRoutes(), nullptr);
  EXPECT_EQ(request_headers_.getHostValue(), "original.host");
  EXPECT_TRUE(request_headers_.getEnvoyMaxRetriesValue().empty());

  EXPECT_EQ(counterValue("model_name_missing"), 1);
  EXPECT_EQ(counterValue("route_not_found"), 0);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(ModelRouterFilterTest, RouteNotFoundForModel) {
  setModelInFilterState("unknown_model");
  run(/*config=*/nullptr, /*expect_local_reply=*/true);
  expectLocalReply(Http::Code::NotFound, "Unknown model");

  EXPECT_EQ(resolvedRoutes(), nullptr);
  EXPECT_EQ(request_headers_.getHostValue(), "original.host");
  EXPECT_TRUE(request_headers_.getEnvoyMaxRetriesValue().empty());

  EXPECT_EQ(counterValue("model_name_missing"), 0);
  EXPECT_EQ(counterValue("route_not_found"), 1);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(ModelRouterFilterTest, SingleTargetRouteResolved) {
  setModelInFilterState("send_me_to_anthropic");
  run();
  EXPECT_FALSE(reply_code_.has_value());

  const auto* routes_obj = resolvedRoutes();
  ASSERT_NE(routes_obj, nullptr);
  ASSERT_EQ(routes_obj->routes().size(), 1);
  EXPECT_EQ(routes_obj->routes()[0].model, "claude-haiku-4-5");
  EXPECT_EQ(routes_obj->routes()[0].provider, "anthropic");
  ASSERT_NE(routes_obj->routes()[0].binding, nullptr);
  EXPECT_EQ(routes_obj->routes()[0].binding->pattern()->source(), "/v1/messages");

  EXPECT_EQ(request_headers_.getHostValue(), "api.anthropic.com");
  EXPECT_EQ(request_headers_.getEnvoyMaxRetriesValue(), "0");

  EXPECT_EQ(counterValue("model_name_missing"), 0);
  EXPECT_EQ(counterValue("route_not_found"), 0);
  EXPECT_EQ(counterValue("success"), 1);
}

TEST_F(ModelRouterFilterTest, MultipleTargetsRouteSetsMaxRetriesAndPrimaryAuthority) {
  setModelInFilterState("multi_fallback_model");
  run();
  EXPECT_FALSE(reply_code_.has_value());

  const auto* routes_obj = resolvedRoutes();
  ASSERT_NE(routes_obj, nullptr);
  ASSERT_EQ(routes_obj->routes().size(), 3);
  EXPECT_EQ(routes_obj->routes()[0].model, "gpt-4o");
  EXPECT_EQ(routes_obj->routes()[1].model, "gpt-4o-mini");
  EXPECT_EQ(routes_obj->routes()[2].model, "gpt-4o-nano");

  EXPECT_EQ(request_headers_.getHostValue(), "primary.llm.example.com:8443");
  EXPECT_EQ(request_headers_.getEnvoyMaxRetriesValue(), "2");
  EXPECT_EQ(counterValue("success"), 1);
}

TEST_F(ModelRouterFilterTest, FiltersOutModelsOverQuota) {
  expectCheckLimits("primary_openai", "gpt-4o", 0, false);
  expectCheckLimits("primary_openai", "gpt-4o-mini", 0, true);
  expectCheckLimits("primary_openai", "gpt-4o-nano", 0, false);

  setModelInFilterState("multi_fallback_model");
  run(defaultConfig(makeMockQuotaSlot()));

  const auto* routes_obj = resolvedRoutes();
  ASSERT_NE(routes_obj, nullptr);
  ASSERT_EQ(routes_obj->routes().size(), 1);
  EXPECT_EQ(routes_obj->routes()[0].model, "gpt-4o-mini");
  EXPECT_EQ(request_headers_.getHostValue(), "primary.llm.example.com:8443");
  EXPECT_EQ(request_headers_.getEnvoyMaxRetriesValue(), "0");
  EXPECT_EQ(counterValue("success"), 1);
  EXPECT_EQ(counterValue("quota_exceeded"), 0);
}

TEST_F(ModelRouterFilterTest, Returns429WhenAllModelsAreOverQuota) {
  expectCheckLimits("primary_openai", "gpt-4o", 0, false);
  expectCheckLimits("primary_openai", "gpt-4o-mini", 0, false);
  expectCheckLimits("primary_openai", "gpt-4o-nano", 0, false);

  setModelInFilterState("multi_fallback_model");
  run(defaultConfig(makeMockQuotaSlot()), /*expect_local_reply=*/true);

  expectLocalReply(Http::Code::TooManyRequests, "Quota exceeded for all candidate models");
  EXPECT_EQ(resolvedRoutes(), nullptr);
  EXPECT_EQ(request_headers_.getHostValue(), "original.host");
  EXPECT_TRUE(request_headers_.getEnvoyMaxRetriesValue().empty());
  EXPECT_EQ(counterValue("quota_exceeded"), 1);
  EXPECT_EQ(counterValue("success"), 0);
}

TEST_F(ModelRouterFilterTest, PassesEstimatedInputTokensFromRequestInfoAndFailsOpenOnError) {
  expectCheckLimits("anthropic", "claude-haiku-4-5", 37,
                    absl::UnavailableError("quota backend unavailable"));

  setModelInFilterState("send_me_to_anthropic");
  setEstimatedInputTokensInMetadata(37);
  run(defaultConfig(makeMockQuotaSlot()));

  const auto* routes_obj = resolvedRoutes();
  ASSERT_NE(routes_obj, nullptr);
  ASSERT_EQ(routes_obj->routes().size(), 1);
  EXPECT_EQ(routes_obj->routes()[0].model, "claude-haiku-4-5");
  EXPECT_EQ(counterValue("success"), 1);
  EXPECT_EQ(counterValue("quota_exceeded"), 0);
}

TEST_F(ModelRouterFilterTest, ConsumesQuotaOnUnaryAndSseResponsesWithTokenUsage) {
  auto config = defaultConfig(makeMockQuotaSlot());
  expectCheckLimits("anthropic", "claude-haiku-4-5", 0, true);
  expectConsumeQuota("anthropic", "claude-haiku-4-5", 120, 45);
  expectConsumeQuota("anthropic", "claude-haiku-4-5", 80, 25);

  setModelInFilterState("send_me_to_anthropic");
  run(config);

  setTokenUsageInMetadata(120, 45);
  EXPECT_EQ(runResponse(/*sse=*/false, config), 1);

  setTokenUsageInMetadata(80, 25);
  EXPECT_EQ(runResponse(/*sse=*/true, config), 1);
}

TEST_F(ModelRouterFilterTest, DoesNotConsumeQuotaOnNon2xxOrMissingResponseCode) {
  auto config = defaultConfig(makeMockQuotaSlot());
  expectCheckLimits("anthropic", "claude-haiku-4-5", 0, true);
  EXPECT_CALL(*mock_quota_, consumeQuota(testing::_, testing::_, testing::_, testing::_)).Times(0);

  setModelInFilterState("send_me_to_anthropic");
  run(config);
  setTokenUsageInMetadata(100, 50);

  // Missing response code (nullopt).
  EXPECT_EQ(runResponse(/*sse=*/false, config, /*response_code=*/std::nullopt), 0);

  // 500 Internal Server Error on unary response.
  EXPECT_EQ(runResponse(/*sse=*/false, config, /*response_code=*/500), 0);

  // 429 Too Many Requests on SSE response.
  EXPECT_EQ(runResponse(/*sse=*/true, config, /*response_code=*/429), 0);
}

TEST_F(ModelRouterFilterTest, ConsumesQuotaUsesAttemptCountForRetriedRoute) {
  auto config = defaultConfig(makeMockQuotaSlot());
  expectCheckLimits("primary_openai", "gpt-4o", 0, true);
  expectCheckLimits("primary_openai", "gpt-4o-mini", 0, true);
  expectCheckLimits("primary_openai", "gpt-4o-nano", 0, true);

  // Attempt 2 corresponds to index 1 (`gpt-4o-mini`).
  expectConsumeQuota("primary_openai", "gpt-4o-mini", 200, 100);

  setModelInFilterState("multi_fallback_model");
  run(config);

  stream_info_.setAttemptCount(2);
  setTokenUsageInMetadata(200, 100);
  EXPECT_EQ(runResponse(/*sse=*/false, config), 1);
}

TEST_F(ModelRouterFilterTest, ConsumesQuotaFallsBackToEstimatedInputTokensWhenTokenUsageMissing) {
  auto config = defaultConfig(makeMockQuotaSlot());
  expectCheckLimits("anthropic", "claude-haiku-4-5", 42, true);
  expectConsumeQuota("anthropic", "claude-haiku-4-5", 42, 0);

  setModelInFilterState("send_me_to_anthropic");
  setEstimatedInputTokensInMetadata(42);
  run(config);

  // Do not set envoy.ai.token_usage metadata; consumeQuota falls back to estimated_input_tokens.
  EXPECT_EQ(runResponse(/*sse=*/false, config), 1);
}

TEST_F(ModelRouterFilterTest, ConsumesQuotaLogsWarningAndSucceedsOnQuotaErrorOrOutOfBoundsAttempt) {
  auto config = defaultConfig(makeMockQuotaSlot());
  expectCheckLimits("anthropic", "claude-haiku-4-5", 0, true);
  expectConsumeQuota("anthropic", "claude-haiku-4-5", 10, 20,
                     absl::InternalError("backend write failed"));

  setModelInFilterState("send_me_to_anthropic");
  Protobuf::Any empty_request_info;
  MessageUtil::packFrom(empty_request_info, envoy::data::ai::v3::RequestInfo{});
  stream_info_.setDynamicTypedMetadata("envoy.ai.request_info", empty_request_info);
  run(config);
  setTokenUsageInMetadata(10, 20);

  // Non-OK status from consumeQuota is logged and does not fail the response.
  EXPECT_EQ(runResponse(/*sse=*/false, config), 1);

  // Out-of-bounds attempt count logs a warning and skips consumeQuota without failing.
  stream_info_.setAttemptCount(5);
  EXPECT_EQ(runResponse(/*sse=*/false, config), 1);
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
