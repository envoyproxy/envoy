#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"

#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/http/ai_filters/model_router/redis_quota.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "hiredis/hiredis.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {
namespace {

namespace Config = envoy::extensions::http::ai_filters::model_router::v3;
using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOk;
using StatusHelpers::IsOkAndHolds;

class FakeRedisServer {
public:
  explicit FakeRedisServer(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    RELEASE_ASSERT(listen_fd_ >= 0, "socket() failed");

    int opt = 1;
    RELEASE_ASSERT(::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0,
                   "setsockopt() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    RELEASE_ASSERT(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0,
                   "bind() failed");
    RELEASE_ASSERT(::listen(listen_fd_, 16) == 0, "listen() failed");

    socklen_t len = sizeof(addr);
    RELEASE_ASSERT(::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0,
                   "getsockname() failed");
    port_ = ntohs(addr.sin_port);

    listen_event_ = dispatcher_.createFileEvent(
        listen_fd_, [this](uint32_t) { return onAccept(); }, Event::FileTriggerType::Level,
        Event::FileReadyType::Read);
  }

  ~FakeRedisServer() {
    closeClient();
    listen_event_.reset();
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
  }

  int port() const { return port_; }
  size_t acceptedConnections() const { return accepted_connections_; }
  const absl::flat_hash_map<std::string, uint64_t>& expirations() const { return expirations_; }

  void closeClient() {
    client_event_.reset();
    if (client_fd_ >= 0) {
      ::close(client_fd_);
      client_fd_ = -1;
    }
    if (reader_ != nullptr) {
      redisReaderFree(reader_);
      reader_ = nullptr;
    }
  }

private:
  absl::Status onAccept() {
    while (true) {
      sockaddr_in client_addr{};
      socklen_t len = sizeof(client_addr);
      const int fd =
          ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len, SOCK_NONBLOCK);
      if (fd < 0) {
        break;
      }
      ++accepted_connections_;
      closeClient();
      client_fd_ = fd;
      reader_ = redisReaderCreate();
      client_event_ = dispatcher_.createFileEvent(
          client_fd_, [this](uint32_t) { return onClientRead(); }, Event::FileTriggerType::Level,
          Event::FileReadyType::Read);
    }
    return absl::OkStatus();
  }

  absl::Status onClientRead() {
    char buf[8192];
    while (client_fd_ >= 0) {
      const ssize_t n = ::recv(client_fd_, buf, sizeof(buf), 0);
      if (n <= 0) {
        if (n == 0) {
          closeClient();
        }
        break;
      }
      RELEASE_ASSERT(redisReaderFeed(reader_, buf, static_cast<size_t>(n)) == REDIS_OK,
                     "redisReaderFeed failed");

      void* raw_reply = nullptr;
      while (client_fd_ >= 0 && redisReaderGetReply(reader_, &raw_reply) == REDIS_OK &&
             raw_reply != nullptr) {
        auto* parsed = static_cast<redisReply*>(raw_reply);
        std::vector<std::string> cmd;
        if (parsed->type == REDIS_REPLY_ARRAY) {
          cmd.reserve(parsed->elements);
          for (size_t i = 0; i < parsed->elements; ++i) {
            cmd.emplace_back(parsed->element[i]->str, parsed->element[i]->len);
          }
        }
        freeReplyObject(raw_reply);
        handleCommand(cmd);
      }
    }
    return absl::OkStatus();
  }

  void sendRawReply(absl::string_view raw) {
    RELEASE_ASSERT(client_fd_ >= 0, "no connected client");
    const ssize_t written = ::send(client_fd_, raw.data(), raw.size(), MSG_NOSIGNAL);
    RELEASE_ASSERT(written == static_cast<ssize_t>(raw.size()), "send() failed");
  }

  void handleCommand(const std::vector<std::string>& cmd) {
    if (cmd.empty()) {
      return;
    }
    const std::string op = absl::AsciiStrToUpper(cmd[0]);
    if (op == "MGET" && cmd.size() >= 2) {
      std::string out = absl::StrCat("*", cmd.size() - 1, "\r\n");
      for (size_t i = 1; i < cmd.size(); ++i) {
        auto it = counters_.find(cmd[i]);
        if (it == counters_.end()) {
          absl::StrAppend(&out, "$-1\r\n");
        } else {
          const std::string s = absl::StrCat(it->second);
          absl::StrAppend(&out, "$", s.size(), "\r\n", s, "\r\n");
        }
      }
      sendRawReply(out);
    } else if (op == "INCRBY" && cmd.size() == 3) {
      int64_t delta = 0;
      RELEASE_ASSERT(absl::SimpleAtoi(cmd[2], &delta), "SimpleAtoi failed");
      const int64_t val = (counters_[cmd[1]] += delta);
      sendRawReply(absl::StrCat(":", val, "\r\n"));
    } else if (op == "EXPIRE" && cmd.size() == 3) {
      uint64_t seconds = 0;
      RELEASE_ASSERT(absl::SimpleAtoi(cmd[2], &seconds), "SimpleAtoi failed");
      expirations_[cmd[1]] = seconds;
      sendRawReply(":1\r\n");
    } else {
      sendRawReply("-ERR unknown command\r\n");
    }
  }

  Event::Dispatcher& dispatcher_;
  int listen_fd_{-1};
  int client_fd_{-1};
  int port_{0};
  size_t accepted_connections_{0};
  Event::FileEventPtr listen_event_;
  Event::FileEventPtr client_event_;
  redisReader* reader_{nullptr};
  absl::flat_hash_map<std::string, int64_t> counters_;
  absl::flat_hash_map<std::string, uint64_t> expirations_;
};

class RedisQuotaTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  RedisQuotaTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("redis_quota_test")),
        executor_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)),
        server_(*dispatcher_) {
    simTime().setSystemTime(std::chrono::seconds(1000000));
  }

  template <typename T> T runTask(Coroutine::Task<T> task) {
    std::optional<T> result;
    bool running = false;
    auto handle = Coroutine::launch(
        std::move(task), executor_,
        [this, &result, &running](T value) {
          result.emplace(std::move(value));
          if (running) {
            dispatcher_->exit();
          }
        },
        Coroutine::StartMode::Inline);
    if (!result.has_value()) {
      running = true;
      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
      running = false;
    }
    RELEASE_ASSERT(result.has_value(), "Task did not complete");
    return std::move(*result);
  }

  template <typename D> void advanceTime(const D& duration) {
    simTime().advanceTimeAndRun(duration, *dispatcher_, Event::Dispatcher::RunType::NonBlock);
  }

  Config::ModelRouter makeProtoWithRedis() const {
    Config::ModelRouter proto;
    proto.mutable_redis()->set_host("127.0.0.1");
    proto.mutable_redis()->set_port(server_.port());
    return proto;
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
  Config::ModelRouter proto = makeProtoWithRedis();
  addModel(proto, "haiku", "anthropic", "claude-haiku-4-5");

  RedisQuota quota(proto, *dispatcher_, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-haiku-4-5", 1000)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 1000)), IsOkAndHolds(true));

  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-haiku-4-5", 500, 500)), IsOk());
  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 500, 500)), IsOk());
  EXPECT_EQ(server_.acceptedConnections(), 0);
}

TEST_F(RedisQuotaTest, ZeroLimitDisablesModelWithoutConnecting) {
  Config::ModelRouter proto = makeProtoWithRedis();
  auto* req_disabled = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*req_disabled, Config::REQUESTS, Config::MINUTE, 0);

  auto* tok_disabled = addModel(proto, "sonnet", "anthropic", "claude-sonnet-4-5");
  addQuota(*tok_disabled, Config::TOKENS, Config::HOUR, 0);

  RedisQuota quota(proto, *dispatcher_, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("openai", "gpt-4o", 0)), IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 0)), IsOkAndHolds(false));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 10)),
              IsOkAndHolds(false));

  EXPECT_THAT(runTask(quota.consumeQuota("openai", "gpt-4o", 10, 10)), IsOk());
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 10, 10)), IsOk());
  EXPECT_EQ(server_.acceptedConnections(), 0);
}

TEST_F(RedisQuotaTest, EnforcesRequestsQuotaAndRollsSubBucketsOverMinuteWindow) {
  Config::ModelRouter proto = makeProtoWithRedis();
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 2);

  RedisQuota quota(proto, *dispatcher_, simTime());

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
  Config::ModelRouter proto = makeProtoWithRedis();
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::TOKENS, Config::MINUTE, 600);

  RedisQuota quota(proto, *dispatcher_, simTime());

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

TEST_F(RedisQuotaTest, EnforcesHourAndDayWindowsAndMultipleQuotas) {
  Config::ModelRouter proto = makeProtoWithRedis();
  auto* model = addModel(proto, "sonnet", "anthropic", "claude-sonnet-4-5");
  addQuota(*model, Config::REQUESTS, Config::HOUR, 1);
  addQuota(*model, Config::TOKENS, Config::DAY, 86400);

  // Align timestamp to a 15-minute (900s) boundary.
  simTime().setSystemTime(std::chrono::seconds(900000));

  RedisQuota quota(proto, *dispatcher_, simTime());

  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 100)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 50, 50)), IsOk());
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 10)),
              IsOkAndHolds(false));

  // Advance 59 minutes: 1-hour request quota (60x1m buckets) is still exhausted.
  advanceTime(std::chrono::minutes(59));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 10)),
              IsOkAndHolds(false));

  // Advance 1 more minute (60 minutes total): hour request quota rolls out, while day token quota
  // still counts the 100 tokens consumed at t = 0.
  advanceTime(std::chrono::minutes(1));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 86300)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 86301)),
              IsOkAndHolds(false));

  // Advance 23 more hours (24 hours total = 96x15m buckets): day token quota rolls out completely.
  advanceTime(std::chrono::hours(23));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 86400)),
              IsOkAndHolds(true));
}

TEST_F(RedisQuotaTest, EnforcesUnitMultiplierWithScaledSubBucketsAndDistinctKeys) {
  Config::ModelRouter proto = makeProtoWithRedis();
  auto* model = addModel(proto, "sonnet", "anthropic", "claude-sonnet-4-5");
  // Configure both a 1-hour request quota (2 req/1h) and a 6-hour request quota (2 req/6h), plus a
  // 6-hour token quota (1000 tok/6h).
  addQuota(*model, Config::REQUESTS, Config::HOUR, 2, /*unit_multiplier=*/1);
  addQuota(*model, Config::REQUESTS, Config::HOUR, 2, /*unit_multiplier=*/6);
  addQuota(*model, Config::TOKENS, Config::HOUR, 1000, /*unit_multiplier=*/6);

  // Align timestamp to a 6-minute (360s) boundary: 1080000 / 360 = 3000, 1080000 / 60 = 18000.
  simTime().setSystemTime(std::chrono::seconds(1080000));

  RedisQuota quota(proto, *dispatcher_, simTime());

  // Consume 1 request (400 tokens) at t = 0, and 1 request (400 tokens) at t + 30m.
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 200, 200)), IsOk());
  advanceTime(std::chrono::minutes(30));
  EXPECT_THAT(runTask(quota.consumeQuota("anthropic", "claude-sonnet-4-5", 200, 200)), IsOk());

  // Verify distinct Redis keys and TTLs for 1h (3600s) vs 6h (21600s).
  EXPECT_EQ(server_.expirations().at("model_router:anthropic:claude-sonnet-4-5:req:1h:18000"),
            3600);
  EXPECT_EQ(server_.expirations().at("model_router:anthropic:claude-sonnet-4-5:req:6h:3000"),
            21600);
  EXPECT_EQ(server_.expirations().at("model_router:anthropic:claude-sonnet-4-5:tok:6h:3000"),
            21600);

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
  // out of the 60-bucket window, leaving 1 request and 400 tokens from t + 30m.
  advanceTime(std::chrono::minutes(6));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 600)),
              IsOkAndHolds(true));
  EXPECT_THAT(runTask(quota.checkLimits("anthropic", "claude-sonnet-4-5", 601)),
              IsOkAndHolds(false));
}

TEST_F(RedisQuotaTest, NonBlockingConcurrentConnectFailsOpenForSecondCaller) {
  Config::ModelRouter proto = makeProtoWithRedis();
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 5);

  RedisQuota quota(proto, *dispatcher_, simTime());

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
  Config::ModelRouter proto = makeProtoWithRedis();
  proto.mutable_redis()->mutable_timeout()->set_seconds(1);
  auto* model = addModel(proto, "gpt4o", "openai", "gpt-4o");
  addQuota(*model, Config::REQUESTS, Config::MINUTE, 2);

  RedisQuota quota(proto, *dispatcher_, simTime());

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
