#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"

#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/http/ai_filters/model_router/async_redis.h"

#include "test/extensions/http/ai_filters/model_router/fake_redis_server.h"
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

using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOk;
using StatusHelpers::IsOkAndHolds;
using testing::ElementsAre;

class AsyncRedisTest : public testing::Test {
protected:
  AsyncRedisTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        exec_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)),
        server_(*dispatcher_) {}

  template <typename T> T runOnDispatcher(Coroutine::Task<T> task) {
    return runTaskOnDispatcher(*dispatcher_, exec_, std::move(task));
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Coroutine::DispatcherExecutor> exec_;
  FakeRedisServer server_;
};

TEST_F(AsyncRedisTest, ConnectPingAndClose) {
  auto task = [this]() -> Coroutine::Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(std::unique_ptr<AsyncRedis> redis,
                        AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port()));
    CO_RETURN_IF_ERROR(co_await redis->connect());
    // Calling connect() again when already connected is a no-op success.
    CO_RETURN_IF_ERROR(co_await redis->connect());
    CO_RETURN_IF_ERROR(co_await redis->ping());
    CO_RETURN_IF_ERROR(co_await redis->close());
    // Calling close() again when already closed is a no-op success.
    CO_RETURN_IF_ERROR(co_await redis->close());
    // Calling connect() after close() fails with UnavailableError.
    EXPECT_THAT(co_await redis->connect(), HasStatusCode(absl::StatusCode::kUnavailable));
    co_return absl::OkStatus();
  };

  EXPECT_THAT(runOnDispatcher(task()), IsOk());
}

TEST_F(AsyncRedisTest, CounterOperationsGetMgetIncrByExpire) {
  auto task = [this]() -> Coroutine::Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(std::unique_ptr<AsyncRedis> redis,
                        AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port()));
    CO_RETURN_IF_ERROR(co_await redis->connect());

    // Missing key returns 0.
    ASSIGN_OR_CO_RETURN(int64_t initial, co_await redis->get("quota:openai:gpt4:req:100"));
    EXPECT_EQ(initial, 0);

    // Default `incrBy` increments by 1.
    ASSIGN_OR_CO_RETURN(int64_t after_incr1, co_await redis->incrBy("quota:openai:gpt4:req:100"));
    EXPECT_EQ(after_incr1, 1);

    // `incrBy` with explicit delta.
    ASSIGN_OR_CO_RETURN(int64_t after_incr_tok,
                        co_await redis->incrBy("quota:openai:gpt4:tok:100", 250));
    EXPECT_EQ(after_incr_tok, 250);
    ASSIGN_OR_CO_RETURN(after_incr_tok, co_await redis->incrBy("quota:openai:gpt4:tok:100", 150));
    EXPECT_EQ(after_incr_tok, 400);

    // `get` existing key.
    ASSIGN_OR_CO_RETURN(int64_t current_tok, co_await redis->get("quota:openai:gpt4:tok:100"));
    EXPECT_EQ(current_tok, 400);

    // `expire` sets TTL.
    CO_RETURN_IF_ERROR(co_await redis->expire("quota:openai:gpt4:tok:100", 60));

    // `mget` with empty keys returns empty vector without contacting Redis.
    std::vector<std::string> empty_keys;
    ASSIGN_OR_CO_RETURN(std::vector<int64_t> empty_res, co_await redis->mget(empty_keys));
    EXPECT_TRUE(empty_res.empty());

    // `mget` with mix of existing and missing keys.
    std::vector<std::string> keys = {
        "quota:openai:gpt4:req:100",
        "quota:openai:gpt4:req:101",
        "quota:openai:gpt4:tok:100",
    };
    ASSIGN_OR_CO_RETURN(std::vector<int64_t> mget_res, co_await redis->mget(keys));
    EXPECT_THAT(mget_res, ElementsAre(1, 0, 400));

    CO_RETURN_IF_ERROR(co_await redis->close());
    co_return absl::OkStatus();
  };

  EXPECT_THAT(runOnDispatcher(task()), IsOk());
  EXPECT_EQ(server_.expirations().at("quota:openai:gpt4:tok:100"), 60);
}

TEST_F(AsyncRedisTest, ConcurrentPipelinedCommands) {
  auto redis_or = AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port());
  ASSERT_OK(redis_or);
  std::unique_ptr<AsyncRedis> redis = std::move(*redis_or);

  EXPECT_THAT(runOnDispatcher(redis->connect()), IsOk());

  int completed = 0;
  std::vector<int64_t> results(3, 0);
  std::vector<Coroutine::DetachedHandle> handles;
  handles.reserve(3);
  for (int i = 0; i < 3; ++i) {
    handles.push_back(Coroutine::launch(
        redis->incrBy("concurrent_counter", (i + 1) * 10), exec_,
        [this, &completed, &results, i](absl::StatusOr<int64_t> res) {
          ASSERT_OK(res);
          results[i] = *res;
          if (++completed == 3) {
            dispatcher_->exit();
          }
        },
        Coroutine::StartMode::Inline));
  }

  if (completed < 3) {
    dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  }
  EXPECT_THAT(results, ElementsAre(10, 30, 60));

  EXPECT_THAT(runOnDispatcher(redis->close()), IsOk());
}

TEST_F(AsyncRedisTest, DestructorWithoutCloseAndCloseBeforeConnect) {
  auto task = [this]() -> Coroutine::Task<absl::Status> {
    // Open and close without ever calling connect().
    {
      ASSIGN_OR_CO_RETURN(std::unique_ptr<AsyncRedis> redis,
                          AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port()));
      CO_RETURN_IF_ERROR(co_await redis->close());
    }

    // Open, connect, run command, and let destructor clean up without calling close().
    {
      ASSIGN_OR_CO_RETURN(std::unique_ptr<AsyncRedis> redis,
                          AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port()));
      CO_RETURN_IF_ERROR(co_await redis->connect());
      CO_RETURN_IF_ERROR(co_await redis->ping());
    }
    co_return absl::OkStatus();
  };

  EXPECT_THAT(runOnDispatcher(task()), IsOk());
}

TEST_F(AsyncRedisTest, ErrorHandlingAndUnexpectedReplies) {
  // 1. Invalid address in open()
  auto bad_open = AsyncRedis::open(*dispatcher_, "", -1);
  EXPECT_THAT(bad_open, HasStatusCode(absl::StatusCode::kUnavailable));

  // 2. Commands before connect() return UnavailableError.
  auto before_connect = [this]() -> Coroutine::Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(std::unique_ptr<AsyncRedis> redis,
                        AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port()));
    EXPECT_THAT(co_await redis->ping(), HasStatusCode(absl::StatusCode::kUnavailable));
    co_return absl::OkStatus();
  };
  EXPECT_THAT(runOnDispatcher(before_connect()), IsOk());

  // 3. Server error and unexpected reply types.
  auto custom_replies = [this]() -> Coroutine::Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(std::unique_ptr<AsyncRedis> redis,
                        AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port()));
    CO_RETURN_IF_ERROR(co_await redis->connect());

    // REDIS_REPLY_ERROR from server.
    server_.setCustomHandler([](const auto&) { return "-ERR quota script failed\r\n"; });
    EXPECT_THAT(co_await redis->get("k"), HasStatusCode(absl::StatusCode::kInternal));

    // Non-numeric string for GET.
    server_.setCustomHandler([](const auto&) { return "$3\r\nabc\r\n"; });
    EXPECT_THAT(co_await redis->get("k"), HasStatusCode(absl::StatusCode::kInternal));

    // Integer reply for GET is accepted.
    server_.setCustomHandler([](const auto&) { return ":42\r\n"; });
    EXPECT_THAT(co_await redis->get("k"), IsOkAndHolds(42));

    // Unexpected reply type for `PING`, `INCRBY`, `EXPIRE`, `MGET`.
    const std::vector<std::string> keys = {"k"};
    server_.setCustomHandler([](const auto&) { return ":1\r\n"; });
    EXPECT_THAT(co_await redis->ping(), HasStatusCode(absl::StatusCode::kInternal));
    EXPECT_THAT(co_await redis->mget(keys), HasStatusCode(absl::StatusCode::kInternal));

    server_.setCustomHandler([](const auto&) { return "+OK\r\n"; });
    EXPECT_THAT(co_await redis->get("k"), HasStatusCode(absl::StatusCode::kInternal));
    EXPECT_THAT(co_await redis->incrBy("k", 1), HasStatusCode(absl::StatusCode::kInternal));
    EXPECT_THAT(co_await redis->expire("k", 10), HasStatusCode(absl::StatusCode::kInternal));

    // `MGET` array containing an error element or non-integer string.
    server_.setCustomHandler([](const auto&) { return "*1\r\n-ERR element error\r\n"; });
    EXPECT_THAT(co_await redis->mget(keys), HasStatusCode(absl::StatusCode::kInternal));

    server_.setCustomHandler([](const auto&) { return "*1\r\n$3\r\nbad\r\n"; });
    EXPECT_THAT(co_await redis->mget(keys), HasStatusCode(absl::StatusCode::kInternal));

    // Peer closes connection mid-command.
    server_.setCustomHandler([this](const auto&) -> std::optional<std::string> {
      server_.closeClient();
      return std::nullopt;
    });
    EXPECT_THAT(co_await redis->get("k"), HasStatusCode(absl::StatusCode::kUnavailable));

    // Subsequent commands on disconnected client fail immediately.
    EXPECT_THAT(co_await redis->get("k"), HasStatusCode(absl::StatusCode::kUnavailable));

    co_return absl::OkStatus();
  };
  EXPECT_THAT(runOnDispatcher(custom_replies()), IsOk());
}

TEST_F(AsyncRedisTest, CoroutineCancellationWhileCommandInFlight) {
  auto redis_or = AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port());
  ASSERT_OK(redis_or);
  std::unique_ptr<AsyncRedis> redis = std::move(*redis_or);

  EXPECT_THAT(runOnDispatcher(redis->connect()), IsOk());

  bool command_received = false;
  server_.setCustomHandler([this, &command_received](const auto&) -> std::optional<std::string> {
    command_received = true;
    dispatcher_->exit();
    return std::nullopt; // Hold reply until after cancellation.
  });

  std::optional<absl::StatusOr<int64_t>> cmd_result;
  auto cmd_handle = Coroutine::launch(
      redis->get("held_key"), exec_,
      [&cmd_result](absl::StatusOr<int64_t> res) { cmd_result.emplace(std::move(res)); },
      Coroutine::StartMode::Inline);

  if (!command_received) {
    dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  }
  EXPECT_TRUE(command_received);
  EXPECT_FALSE(cmd_result.has_value());

  // Cancel the suspended command coroutine.
  cmd_handle.cancel();
  EXPECT_TRUE(cmd_result.has_value());
  EXPECT_THAT(*cmd_result, HasStatusCode(absl::StatusCode::kCancelled));

  // Now send the delayed reply from server and run follow-up command on same connection.
  server_.setCustomHandler(nullptr);
  server_.sendRawReply("$2\r\n99\r\n");

  EXPECT_THAT(runOnDispatcher(redis->ping()), IsOk());
  EXPECT_THAT(runOnDispatcher(redis->close()), IsOk());
}

TEST_F(AsyncRedisTest, CommandTimeoutDisconnectsAndFailsPendingCommand) {
  auto task = [this]() -> Coroutine::Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(
        std::unique_ptr<AsyncRedis> redis,
        AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port(), std::chrono::milliseconds(50)));
    CO_RETURN_IF_ERROR(co_await redis->connect());

    // Server never replies to the command.
    server_.setCustomHandler(
        [](const auto&) -> std::optional<std::string> { return std::nullopt; });

    EXPECT_THAT(co_await redis->get("stalled_key"), HasStatusCode(absl::StatusCode::kUnavailable));
    // Connection was closed by timeout; subsequent commands fail immediately.
    EXPECT_THAT(co_await redis->ping(), HasStatusCode(absl::StatusCode::kUnavailable));
    co_return absl::OkStatus();
  };

  EXPECT_THAT(runOnDispatcher(task()), IsOk());
}

TEST_F(AsyncRedisTest, ConnectFailureConcurrentConnectAndConnectCancellation) {
  // 1. Concurrent connect() and cancellation while connect() is in flight.
  {
    auto redis_or = AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port());
    ASSERT_OK(redis_or);
    std::unique_ptr<AsyncRedis> redis = std::move(*redis_or);

    std::optional<absl::Status> first_connect;
    auto h1 = Coroutine::launch(
        redis->connect(), exec_,
        [&first_connect](absl::Status s) { first_connect.emplace(std::move(s)); },
        Coroutine::StartMode::Inline);
    EXPECT_FALSE(first_connect.has_value());

    // Second connect while the first is in progress fails immediately.
    EXPECT_THAT(runOnDispatcher(redis->connect()),
                HasStatusCode(absl::StatusCode::kFailedPrecondition));

    // Cancel the in-flight connect coroutine; when the socket event fires, onConnect is a no-op.
    h1.cancel();
    ASSERT_TRUE(first_connect.has_value());
    EXPECT_THAT(*first_connect, HasStatusCode(absl::StatusCode::kCancelled));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // 2. Asynchronous connect failure to a port that refuses connections.
  {
    const int unused_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    RELEASE_ASSERT(unused_fd >= 0, "socket() failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    RELEASE_ASSERT(::bind(unused_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0,
                   "bind() failed");
    socklen_t len = sizeof(addr);
    RELEASE_ASSERT(::getsockname(unused_fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0,
                   "getsockname() failed");
    const int refused_port = ntohs(addr.sin_port);

    auto redis_or = AsyncRedis::open(*dispatcher_, "127.0.0.1", refused_port);
    ASSERT_OK(redis_or);
    std::unique_ptr<AsyncRedis> redis = std::move(*redis_or);

    EXPECT_THAT(runOnDispatcher(redis->connect()), HasStatusCode(absl::StatusCode::kUnavailable));
    ::close(unused_fd);
  }
}

TEST_F(AsyncRedisTest, CloseWhileCommandInFlightConcurrentCancelAndDisconnectError) {
  // 1. Concurrent close() and close() cancellation while waiting for in-flight command to drain.
  {
    auto redis_or = AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port());
    ASSERT_OK(redis_or);
    std::unique_ptr<AsyncRedis> redis = std::move(*redis_or);
    EXPECT_THAT(runOnDispatcher(redis->connect()), IsOk());

    bool command_received = false;
    server_.setCustomHandler([this, &command_received](const auto&) -> std::optional<std::string> {
      command_received = true;
      dispatcher_->exit();
      return std::nullopt;
    });

    std::optional<absl::StatusOr<int64_t>> cmd_result;
    auto cmd_handle = Coroutine::launch(
        redis->get("held_key"), exec_,
        [this, &cmd_result](absl::StatusOr<int64_t> res) {
          cmd_result.emplace(std::move(res));
          dispatcher_->exit();
        },
        Coroutine::StartMode::Inline);
    if (!command_received) {
      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    }
    EXPECT_TRUE(command_received);

    // Start close() while `held_key` is still pending; hiredis defers disconnect until replies
    // drain.
    std::optional<absl::Status> close_result;
    auto close_handle = Coroutine::launch(
        redis->close(), exec_,
        [&close_result](absl::Status s) { close_result.emplace(std::move(s)); },
        Coroutine::StartMode::Inline);
    EXPECT_FALSE(close_result.has_value());

    // Second close() while first close() is in progress fails with FailedPrecondition.
    EXPECT_THAT(runOnDispatcher(redis->close()),
                HasStatusCode(absl::StatusCode::kFailedPrecondition));

    // Cancel the first close() coroutine, then send the pending reply so hiredis finishes
    // disconnecting with `self == nullptr`.
    close_handle.cancel();
    ASSERT_TRUE(close_result.has_value());
    EXPECT_THAT(*close_result, HasStatusCode(absl::StatusCode::kCancelled));

    server_.setCustomHandler(nullptr);
    server_.sendRawReply("$2\r\n42\r\n");
    if (!cmd_result.has_value()) {
      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    }
    EXPECT_THAT(*cmd_result, IsOkAndHolds(42));
  }

  // 2. Close while command is in flight and peer drops the connection before replying.
  {
    auto redis_or = AsyncRedis::open(*dispatcher_, "127.0.0.1", server_.port());
    ASSERT_OK(redis_or);
    std::unique_ptr<AsyncRedis> redis = std::move(*redis_or);
    EXPECT_THAT(runOnDispatcher(redis->connect()), IsOk());

    bool command_received = false;
    server_.setCustomHandler([this, &command_received](const auto&) -> std::optional<std::string> {
      command_received = true;
      dispatcher_->exit();
      return std::nullopt;
    });

    std::optional<absl::StatusOr<int64_t>> cmd_result;
    auto cmd_handle = Coroutine::launch(
        redis->get("held_key"), exec_,
        [&cmd_result](absl::StatusOr<int64_t> res) { cmd_result.emplace(std::move(res)); },
        Coroutine::StartMode::Inline);
    if (!command_received) {
      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    }
    EXPECT_TRUE(command_received);

    std::optional<absl::Status> close_result;
    auto close_handle = Coroutine::launch(
        redis->close(), exec_,
        [this, &close_result](absl::Status s) {
          close_result.emplace(std::move(s));
          dispatcher_->exit();
        },
        Coroutine::StartMode::Inline);
    EXPECT_FALSE(close_result.has_value());

    // Peer closes connection before replying, causing onDisconnect to receive status != REDIS_OK.
    server_.closeClient();
    if (!close_result.has_value()) {
      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    }
    EXPECT_THAT(*close_result, HasStatusCode(absl::StatusCode::kUnavailable));
    ASSERT_TRUE(cmd_result.has_value());
    EXPECT_THAT(*cmd_result, HasStatusCode(absl::StatusCode::kUnavailable));
  }
}

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
