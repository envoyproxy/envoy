#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"

#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/http/ai_filters/model_router/redis_quota.h"

#include "test/extensions/http/ai_filters/model_router/fake_redis_server.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {
namespace {

namespace Config = envoy::extensions::http::ai_filters::model_router::v3;
using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOk;
using StatusHelpers::IsOkAndHolds;

class RedisQuotaTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  RedisQuotaTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("redis_quota_test")),
        executor_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)),
        server_(*dispatcher_) {
    simTime().setSystemTime(std::chrono::seconds(1000000));
  }

  template <typename T> T runTask(Coroutine::Task<T> task) {
    return runTaskOnDispatcher(*dispatcher_, executor_, std::move(task));
  }

  template <typename D> void advanceTime(const D& duration) {
    simTime().advanceTimeAndRun(duration, *dispatcher_, Event::Dispatcher::RunType::NonBlock);
  }

  Config::RedisQuotaConfig makeRedisConfig() const {
    Config::RedisQuotaConfig redis_config;
    redis_config.set_host("127.0.0.1");
    redis_config.set_port(server_.port());
    return redis_config;
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

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Coroutine::Executor> executor_;
  FakeRedisServer server_;
};

TEST_F(RedisQuotaTest, AllowsUnconfiguredModelAndModelWithoutQuotas) {
  Config::ModelRouter proto;
  addModel(proto, "haiku", "anthropic", "claude-haiku-4-5");

  RedisQuota quota(proto, makeRedisConfig(), *dispatcher_, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-haiku-4-5", 1000)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 1000)), IsOkAndHolds(true));

  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-haiku-4-5", 500, 500)), IsOk());
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 500, 500)), IsOk());
  EXPECT_EQ(server_.acceptedConnections(), 0);
}

TEST_F(RedisQuotaTest, ZeroLimitDisablesModelWithoutConnecting) {
  Config::ModelRouter proto;
  auto* req_disabled = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*req_disabled, Config::REQUESTS, Config::MINUTE, 0);

  auto* tok_disabled = addModel(proto, "sonnet", "anthropic", "claude-sonnet-4-5");
  addQuota(*tok_disabled, Config::TOKENS, Config::HOUR, 0);

  RedisQuota quota(proto, makeRedisConfig(), *dispatcher_, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 0)), IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 10)),
              IsOkAndHolds(false));

  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 10, 10)), IsOk());
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 10, 10)), IsOk());
  EXPECT_EQ(server_.acceptedConnections(), 0);
}

TEST_F(RedisQuotaTest, EnforcesRequestsQuotaAndRollsSubBucketsOverMinuteWindow) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 2);

  RedisQuota quota(proto, makeRedisConfig(), *dispatcher_, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 0, 0)), IsOk());
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));

  advanceTime(std::chrono::seconds(30));
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 0, 0)), IsOk());

  // 2 requests consumed in the last 60 seconds: quota is reached.
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));

  // At t + 59s, the first request at t = 0 is still in the 60x1s rolling window.
  advanceTime(std::chrono::seconds(29));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));

  // At t + 60s, the sub-bucket at t = 0 rolls out of the 60s window.
  advanceTime(std::chrono::seconds(1));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));

  EXPECT_EQ(server_.expirations().at("model_router:openai:gpt-4o:req:1m:1000000"), 60);
}

TEST_F(RedisQuotaTest, EnforcesTokensQuotaAndHandlesNegativeDebt) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::TOKENS, Config::MINUTE, 600);

  RedisQuota quota(proto, makeRedisConfig(), *dispatcher_, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 600)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 601)), IsOkAndHolds(false));

  // Consume 500 tokens at t = 0, leaving 100 tokens.
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 200, 300)), IsOk());
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 100)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 101)), IsOkAndHolds(false));

  // Consume 300 tokens at t + 20s, putting total usage in the 60s window at 800 (> 600 limit).
  advanceTime(std::chrono::seconds(20));
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 100, 200)), IsOk());

  // Even when estimated_input_tokens == 0, an over-limit window denies the request.
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));

  // At t + 60s, the 500 tokens from t = 0 roll out, leaving 300 tokens from t + 20s.
  advanceTime(std::chrono::seconds(40));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 300)), IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 301)), IsOkAndHolds(false));
}

TEST_F(RedisQuotaTest, EnforcesHourDayAndUnitMultiplierWindows) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "sonnet", "anthropic", "claude-sonnet-4-5");
  // Configure a 1-hour request quota (2 req/1h), a 6-hour request quota (2 req/6h), and a 1-day
  // token quota (86400 tok/1d).
  addQuota(*model, Config::REQUESTS, Config::HOUR, 2, /*unit_multiplier=*/1);
  addQuota(*model, Config::REQUESTS, Config::HOUR, 2, /*unit_multiplier=*/6);
  addQuota(*model, Config::TOKENS, Config::DAY, 86400);

  // Align timestamp to both 6-minute (360s) and 15-minute (900s) boundaries:
  // 1080000 / 60 = 18000, 1080000 / 360 = 3000, 1080000 / 900 = 1200.
  simTime().setSystemTime(std::chrono::seconds(1080000));

  RedisQuota quota(proto, makeRedisConfig(), *dispatcher_, simTime());

  // Consume 1 request (200 tokens) at t = 0, and 1 request (200 tokens) at t + 30m.
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 100, 100)), IsOk());
  advanceTime(std::chrono::minutes(30));
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 100, 100)), IsOk());

  // Verify distinct Redis keys and TTLs for 1h (3600s), 6h (21600s), and 1d (86400s).
  EXPECT_EQ(server_.expirations().at("model_router:anthropic:claude-sonnet-4-5:req:1h:18000"),
            3600);
  EXPECT_EQ(server_.expirations().at("model_router:anthropic:claude-sonnet-4-5:req:6h:3000"),
            21600);
  EXPECT_EQ(server_.expirations().at("model_router:anthropic:claude-sonnet-4-5:tok:1d:1200"),
            86400);

  // Both 1h and 6h request quotas are now at 2/2.
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 100)),
              IsOkAndHolds(false));

  // Advance to t + 90m: the 1-hour quota (60x1m buckets) has rolled out both requests, but the
  // 6-hour quota (60x6m buckets) still holds both requests.
  advanceTime(std::chrono::minutes(60));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 100)),
              IsOkAndHolds(false));

  // Advance to t + 5h54m (354m total, 59 x 6m buckets since t = 0): first request at t = 0 is in
  // bucket 3000, current bucket is 3059 -> still within the 60-bucket (6h) window.
  advanceTime(std::chrono::minutes(264));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 100)),
              IsOkAndHolds(false));

  // Advance 6 more minutes to t + 6h (360m total, bucket 3060): bucket 3000 (from t = 0) rolls
  // out of the 60-bucket 6h window, while the 1-day token quota (96x15m buckets) still counts all
  // 400 tokens consumed at t = 0 and t + 30m.
  advanceTime(std::chrono::minutes(6));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 86000)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 86001)),
              IsOkAndHolds(false));

  // Advance 18 more hours to t + 24h30m: all 1-day token sub-buckets roll out completely.
  advanceTime(std::chrono::hours(18) + std::chrono::minutes(30));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 86400)),
              IsOkAndHolds(true));
}

TEST_F(RedisQuotaTest, NonBlockingConcurrentConnectFailsOpenForSecondCaller) {
  Config::ModelRouter proto;
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 5);

  RedisQuota quota(proto, makeRedisConfig(), *dispatcher_, simTime());

  auto task = [&]() -> Coroutine::Task<absl::Status> {
    std::optional<absl::StatusOr<bool>> first_result;
    std::optional<absl::StatusOr<bool>> second_result;

    // First call starts connecting asynchronously.
    auto h1 = Coroutine::launch(
        quota.checkLimits("openai", "gpt-4o", 0), executor_,
        [&](absl::StatusOr<bool> res) { first_result.emplace(std::move(res)); },
        Coroutine::StartMode::Inline);

    // Second call while the first is still connecting immediately returns UnavailableError.
    auto h2 = Coroutine::launch(
        quota.checkLimits("openai", "gpt-4o", 0), executor_,
        [&](absl::StatusOr<bool> res) { second_result.emplace(std::move(res)); },
        Coroutine::StartMode::Inline);

    EXPECT_TRUE(second_result.has_value());
    EXPECT_THAT(*second_result, HasStatusCode(absl::StatusCode::kUnavailable));

    // Once the event loop runs, the first call finishes connecting and succeeds.
    while (!first_result.has_value()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    EXPECT_THAT(*first_result, IsOkAndHolds(true));
    co_return absl::OkStatus();
  };

  EXPECT_THAT(runTask(task()), IsOk());
}

TEST_F(RedisQuotaTest, AutoReconnectsAfterServerDisconnect) {
  Config::ModelRouter proto;
  Config::RedisQuotaConfig redis_config = makeRedisConfig();
  redis_config.mutable_timeout()->set_seconds(1);
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 2);

  RedisQuota quota(proto, redis_config, *dispatcher_, simTime());

  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 0, 0)), IsOk());
  EXPECT_EQ(server_.acceptedConnections(), 1);

  // Simulate Redis dropping the connection.
  server_.closeClient();

  // First call after peer disconnect observes the closed socket and resets the client.
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)),
              HasStatusCode(absl::StatusCode::kUnavailable));

  // Next call automatically reconnects and reads the previously stored counter.
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(true));
  EXPECT_EQ(server_.acceptedConnections(), 2);

  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 0, 0)), IsOk());
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
