#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/tracing/tracer.h"
#include "envoy/type/v3/ratelimit_unit.pb.h"

#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"
#include "source/common/grpc/common.h"
#include "source/extensions/http/ai_filters/model_router/rls_quota.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/mocks/grpc/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {
namespace {

namespace Config = envoy::extensions::http::ai_filters::model_router::v3;
using envoy::extensions::common::ratelimit::v3::RateLimitDescriptor;
using envoy::service::ratelimit::v3::RateLimitRequest;
using envoy::service::ratelimit::v3::RateLimitResponse;
using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOk;
using StatusHelpers::IsOkAndHolds;
using testing::_;
using testing::StrictMock;

class RlsQuotaTest : public testing::Test {
protected:
  RlsQuotaTest() : mock_client_(std::make_shared<StrictMock<Grpc::MockAsyncClient>>()) {}

  template <typename T> T runTask(Coroutine::Task<T> task) {
    auto exec = std::make_shared<Coroutine::ManualExecutor>();
    std::optional<T> result;
    auto handle = Coroutine::launch(
        std::move(task), exec, [&result](T val) { result.emplace(std::move(val)); },
        Coroutine::StartMode::Inline);
    exec->drain();
    RELEASE_ASSERT(result.has_value(), "Task did not complete synchronously");
    return std::move(*result);
  }

  static Config::Model* addModel(Config::ModelRouter& proto, const std::string& name,
                                 const std::string& provider, const std::string& model) {
    auto* m = proto.add_models();
    m->set_name(name);
    m->set_provider(provider);
    m->set_model(model);
    return m;
  }

  static void addQuota(Config::Model& model, Config::QuotaType type, Config::TimeUnit unit,
                       uint32_t limit, uint32_t unit_multiplier = 0) {
    auto* q = model.add_quotas();
    q->set_quota_type(type);
    q->set_time_unit(unit);
    q->set_limit(limit);
    if (unit_multiplier != 0) {
      q->mutable_unit_multiplier()->set_value(unit_multiplier);
    }
  }

  static void
  addExpectedDescriptor(RateLimitRequest& req, const std::string& provider,
                        const std::string& model, const std::string& quota_type,
                        uint64_t hits_addend,
                        std::optional<envoy::type::v3::RateLimitUnit> unit = std::nullopt,
                        uint32_t limit = 0, uint32_t unit_multiplier = 1) {
    auto* desc = req.add_descriptors();
    auto* e1 = desc->add_entries();
    e1->set_key("provider");
    e1->set_value(provider);
    auto* e2 = desc->add_entries();
    e2->set_key("model");
    e2->set_value(model);
    auto* e3 = desc->add_entries();
    e3->set_key("quota_type");
    e3->set_value(quota_type);
    desc->mutable_hits_addend()->set_value(hits_addend);
    if (unit.has_value()) {
      auto* lim = desc->mutable_limit();
      lim->set_requests_per_unit(limit);
      lim->set_unit(*unit);
      if (unit_multiplier != 1) {
        lim->mutable_unit_multiplier()->set_value(unit_multiplier);
      }
    }
  }

  void expectRlsCall(absl::string_view expected_method, const RateLimitRequest& expected_req,
                     RateLimitResponse::Code response_code = RateLimitResponse::OK,
                     std::chrono::milliseconds expected_timeout = AsyncRlsClient::DefaultTimeout) {
    EXPECT_CALL(*mock_client_,
                sendRaw("envoy.service.ratelimit.v3.RateLimitService", expected_method, _, _, _, _))
        .WillOnce([expected_req, response_code, expected_timeout](
                      absl::string_view, absl::string_view, Buffer::InstancePtr&& request,
                      Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span& span,
                      const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
          EXPECT_EQ(options.timeout, expected_timeout);
          RateLimitRequest actual_req;
          EXPECT_TRUE(actual_req.ParseFromString(request->toString()));
          EXPECT_TRUE(TestUtility::protoEqual(actual_req, expected_req))
              << "Actual:\n"
              << actual_req.DebugString() << "Expected:\n"
              << expected_req.DebugString();

          RateLimitResponse resp;
          resp.set_overall_code(response_code);
          callbacks.onSuccessRaw(Grpc::Common::serializeMessage(resp), span);
          return nullptr;
        });
  }

  std::shared_ptr<StrictMock<Grpc::MockAsyncClient>> mock_client_;
};

TEST_F(RlsQuotaTest, QueriesRlsWithDefaultDescriptorsWhenModelQuotasNotConfigured) {
  Config::ModelRouter proto;
  addModel(proto, "haiku", "anthropic", "claude-haiku-4-5");
  Config::RlsQuotaConfig rls_config;

  RlsQuota quota(proto, rls_config, mock_client_);

  // 1. checkLimits with estimated_input_tokens = 250.
  RateLimitRequest expected_check;
  expected_check.set_domain("model_router");
  addExpectedDescriptor(expected_check, "anthropic", "claude-haiku-4-5", "requests", 1);
  addExpectedDescriptor(expected_check, "anthropic", "claude-haiku-4-5", "tokens", 250);
  expectRlsCall("CheckRateLimit", expected_check, RateLimitResponse::OK);
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-haiku-4-5", 250)), IsOkAndHolds(true));

  // 2. checkLimits with estimated_input_tokens = 0 uses 1 token minimum.
  RateLimitRequest expected_check_zero;
  expected_check_zero.set_domain("model_router");
  addExpectedDescriptor(expected_check_zero, "anthropic", "claude-haiku-4-5", "requests", 1);
  addExpectedDescriptor(expected_check_zero, "anthropic", "claude-haiku-4-5", "tokens", 1);
  expectRlsCall("CheckRateLimit", expected_check_zero, RateLimitResponse::OVER_LIMIT);
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-haiku-4-5", 0)), IsOkAndHolds(false));

  // 3. consumeQuota with non-zero tokens sends both descriptors.
  RateLimitRequest expected_consume;
  expected_consume.set_domain("model_router");
  addExpectedDescriptor(expected_consume, "anthropic", "claude-haiku-4-5", "requests", 1);
  addExpectedDescriptor(expected_consume, "anthropic", "claude-haiku-4-5", "tokens", 300);
  expectRlsCall("ConsumeRateLimit", expected_consume);
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-haiku-4-5", 100, 200)), IsOk());

  // 4. consumeQuota with 0 tokens sends only the requests descriptor.
  RateLimitRequest expected_consume_zero_tok;
  expected_consume_zero_tok.set_domain("model_router");
  addExpectedDescriptor(expected_consume_zero_tok, "anthropic", "claude-haiku-4-5", "requests", 1);
  expectRlsCall("ConsumeRateLimit", expected_consume_zero_tok);
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-haiku-4-5", 0, 0)), IsOk());
}

TEST_F(RlsQuotaTest, AttachesRateLimitOverridesWhenModelQuotasConfigured) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 10);
  addQuota(*model, Config::REQUESTS, Config::HOUR, 100, 2);
  addQuota(*model, Config::TOKENS, Config::DAY, 50000);

  Config::RlsQuotaConfig rls_config;
  rls_config.set_domain("ai_gateway");
  rls_config.mutable_timeout()->set_seconds(2);

  RlsQuota quota(proto, rls_config, mock_client_);

  RateLimitRequest expected_check;
  expected_check.set_domain("ai_gateway");
  addExpectedDescriptor(expected_check, "openai", "gpt-4o", "requests", 1, envoy::type::v3::MINUTE,
                        10, 1);
  addExpectedDescriptor(expected_check, "openai", "gpt-4o", "requests", 1, envoy::type::v3::HOUR,
                        100, 2);
  addExpectedDescriptor(expected_check, "openai", "gpt-4o", "tokens", 500, envoy::type::v3::DAY,
                        50000, 1);
  expectRlsCall("CheckRateLimit", expected_check, RateLimitResponse::OK,
                std::chrono::milliseconds(2000));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 500)), IsOkAndHolds(true));

  RateLimitRequest expected_consume;
  expected_consume.set_domain("ai_gateway");
  addExpectedDescriptor(expected_consume, "openai", "gpt-4o", "requests", 1,
                        envoy::type::v3::MINUTE, 10, 1);
  addExpectedDescriptor(expected_consume, "openai", "gpt-4o", "requests", 1, envoy::type::v3::HOUR,
                        100, 2);
  addExpectedDescriptor(expected_consume, "openai", "gpt-4o", "tokens", 750, envoy::type::v3::DAY,
                        50000, 1);
  expectRlsCall("ConsumeRateLimit", expected_consume, RateLimitResponse::OK,
                std::chrono::milliseconds(2000));
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 500, 250)), IsOk());

  // Consuming with 0 tokens omits the token descriptor.
  RateLimitRequest expected_consume_zero_tokens;
  expected_consume_zero_tokens.set_domain("ai_gateway");
  addExpectedDescriptor(expected_consume_zero_tokens, "openai", "gpt-4o", "requests", 1,
                        envoy::type::v3::MINUTE, 10, 1);
  addExpectedDescriptor(expected_consume_zero_tokens, "openai", "gpt-4o", "requests", 1,
                        envoy::type::v3::HOUR, 100, 2);
  expectRlsCall("ConsumeRateLimit", expected_consume_zero_tokens, RateLimitResponse::OK,
                std::chrono::milliseconds(2000));
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 0, 0)), IsOk());
}

TEST_F(RlsQuotaTest, ZeroLimitDisablesModelWithoutRpc) {
  Config::ModelRouter proto;
  auto* req_disabled = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*req_disabled, Config::REQUESTS, Config::MINUTE, 0);

  auto* tok_disabled = addModel(proto, "sonnet", "anthropic", "claude-sonnet-4-5");
  addQuota(*tok_disabled, Config::TOKENS, Config::HOUR, 0);

  Config::RlsQuotaConfig rls_config;
  RlsQuota quota(proto, rls_config, mock_client_);

  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 100)), IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 100)),
              IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 10, 10)), IsOk());
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 10, 10)), IsOk());
}

TEST_F(RlsQuotaTest, ModelWithOnlyTokenQuotasZeroTokensConsumeIsNoOp) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::TOKENS, Config::MINUTE, 1000);

  Config::RlsQuotaConfig rls_config;
  RlsQuota quota(proto, rls_config, mock_client_);

  // No request quotas and 0 tokens consumed -> no RPC issued.
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 0, 0)), IsOk());
}

TEST_F(RlsQuotaTest, RpcFailurePropagatesError) {
  Config::ModelRouter proto;
  Config::RlsQuotaConfig rls_config;
  RlsQuota quota(proto, rls_config, mock_client_);

  EXPECT_CALL(*mock_client_,
              sendRaw("envoy.service.ratelimit.v3.RateLimitService", "CheckRateLimit", _, _, _, _))
      .WillOnce([](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                   Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span& span,
                   const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
        callbacks.onFailure(Grpc::Status::WellKnownGrpcStatus::Unavailable, "RLS down", span);
        return nullptr;
      });
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 10)),
              HasStatusCode(absl::StatusCode::kUnavailable));

  EXPECT_CALL(*mock_client_, sendRaw("envoy.service.ratelimit.v3.RateLimitService",
                                     "ConsumeRateLimit", _, _, _, _))
      .WillOnce([](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                   Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span& span,
                   const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
        callbacks.onFailure(Grpc::Status::WellKnownGrpcStatus::Internal, "RLS error", span);
        return nullptr;
      });
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 10, 5)),
              HasStatusCode(absl::StatusCode::kInternal));
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
