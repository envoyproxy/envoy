#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"

#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/http/ai_filters/model_router/local_quota.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {
namespace {

namespace Config = envoy::extensions::http::ai_filters::model_router::v3;
using StatusHelpers::IsOk;
using StatusHelpers::IsOkAndHolds;

class LocalQuotaTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
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
};

TEST_F(LocalQuotaTest, AllowsUnconfiguredModelAndModelWithoutQuotas) {
  Config::ModelRouter proto;
  addModel(proto, "haiku", "anthropic", "claude-haiku-4-5");

  LocalQuota quota(proto, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-haiku-4-5", 1000)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 1000)), IsOkAndHolds(true));

  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-haiku-4-5", 500, 500)), IsOk());
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 500, 500)), IsOk());
}

TEST_F(LocalQuotaTest, ZeroLimitDisablesModel) {
  Config::ModelRouter proto;
  auto* req_disabled = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*req_disabled, Config::REQUESTS, Config::MINUTE, 0);

  auto* tok_disabled = addModel(proto, "sonnet", "anthropic", "claude-sonnet-4-5");
  addQuota(*tok_disabled, Config::TOKENS, Config::HOUR, 0);

  LocalQuota quota(proto, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 0)), IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 10)),
              IsOkAndHolds(false));

  // Consuming on a disabled model is a safe no-op and remains disabled after time advances.
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 10, 10)), IsOk());
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 10, 10)), IsOk());
  simTime().advanceTimeWait(std::chrono::hours(2));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 0)), IsOkAndHolds(false));
}

TEST_F(LocalQuotaTest, EnforcesRequestsQuotaAndRefillsOverMinuteWindow) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 2);

  LocalQuota quota(proto, simTime());

  // checkLimits does not consume tokens by itself.
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));

  // Consume 2 requests.
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 0, 0)), IsOk());
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 0, 0)), IsOk());

  // Bucket is now at 0 remaining tokens.
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));

  // With 2 requests per 60s, 1 request refills after 30s.
  simTime().advanceTimeWait(std::chrono::seconds(29));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));

  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));
}

TEST_F(LocalQuotaTest, EnforcesTokensQuotaAndHandlesNegativeDebt) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  // 600 tokens per minute = 10 tokens per second.
  addQuota(*model, Config::TOKENS, Config::MINUTE, 600);

  LocalQuota quota(proto, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 600)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 601)), IsOkAndHolds(false));

  // Consume 500 tokens (200 input + 300 output), leaving 100 tokens.
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 200, 300)), IsOk());
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 100)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 101)), IsOkAndHolds(false));

  // Consume 300 tokens (100 input + 200 output), putting bucket at -200 tokens (debt).
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 100, 200)), IsOk());

  // Even when estimated_input_tokens == 0, an exhausted/debt bucket denies the request.
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));

  // At 10 tokens/sec, after 20s the bucket reaches 0 tokens (still < 1 required token).
  simTime().advanceTimeWait(std::chrono::seconds(20));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));

  // After 1 more second (21s total), bucket has 10 tokens.
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 10)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 11)), IsOkAndHolds(false));
}

TEST_F(LocalQuotaTest, EnforcesHourDayAndUnitMultiplierWindows) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "sonnet", "anthropic", "claude-sonnet-4-5");
  // 2 requests per 6 hours (1 request refills every 3 hours) and 86400 tokens per day (1
  // token/sec).
  addQuota(*model, Config::REQUESTS, Config::HOUR, 2, /*unit_multiplier=*/6);
  addQuota(*model, Config::TOKENS, Config::DAY, 86400);

  LocalQuota quota(proto, simTime());

  // Consume both requests and 21600 tokens.
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 5000, 5800)), IsOk());
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 5000, 5800)), IsOk());
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 10)),
              IsOkAndHolds(false));

  // After 1 hour (when a 1-hour quota would have refilled), 6-hour request quota is still < 1.0.
  simTime().advanceTimeWait(std::chrono::hours(1));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 10)),
              IsOkAndHolds(false));

  // After 2 hours and 59 minutes total, request bucket is still slightly below 1.0.
  simTime().advanceTimeWait(std::chrono::hours(1) + std::chrono::minutes(59));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 10)),
              IsOkAndHolds(false));

  // At 3 hours total, 1 request has refilled and 3 hours * 3600 = 10800 tokens have refilled in the
  // 1-day token bucket (64800 remaining + 10800 = 75600 available).
  simTime().advanceTimeWait(std::chrono::minutes(1));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 75600)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 75601)),
              IsOkAndHolds(false));
}

TEST_F(LocalQuotaTest, SharesStateAcrossCopiedInstances) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "gemini_pro", "gemini", "gemini-2.5-pro");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 1);

  LocalQuota base_quota(proto, simTime());
  LocalQuota thread_1_quota(base_quota);
  LocalQuota thread_2_quota(base_quota);

  EXPECT_THAT(runTask(thread_1_quota.checkLimits("gemini", "gemini-2.5-pro", 0)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(thread_2_quota.checkLimits("gemini", "gemini-2.5-pro", 0)),
              IsOkAndHolds(true));

  // Consuming on thread 1 immediately affects thread 2.
  EXPECT_THAT(runTask(thread_1_quota.consumeQuota("gemini", "gemini-2.5-pro", 10, 20)), IsOk());
  EXPECT_THAT(runTask(thread_2_quota.checkLimits("gemini", "gemini-2.5-pro", 0)),
              IsOkAndHolds(false));
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
