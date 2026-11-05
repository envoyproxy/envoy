#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/http/ai_filters/model_router/async_redis.h"

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

using StatusHelpers::HasStatusCode;
using StatusHelpers::IsOk;
using StatusHelpers::IsOkAndHolds;
using testing::ElementsAre;

// Lightweight in-process fake Redis server running on the test Event::Dispatcher.
class FakeRedisServer {
public:
  using CommandHandler = std::function<std::optional<std::string>(const std::vector<std::string>&)>;

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
    if (reader_ != nullptr) {
      redisReaderFree(reader_);
      reader_ = nullptr;
    }
  }

  int port() const { return port_; }

  void setCustomHandler(CommandHandler handler) { custom_handler_ = std::move(handler); }

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

  void sendRawReply(absl::string_view raw) {
    RELEASE_ASSERT(client_fd_ >= 0, "no connected client");
    const ssize_t written = ::send(client_fd_, raw.data(), raw.size(), MSG_NOSIGNAL);
    RELEASE_ASSERT(written == static_cast<ssize_t>(raw.size()), "send() failed");
  }

  const absl::flat_hash_map<std::string, uint64_t>& expirations() const { return expirations_; }

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
    char buf[4096];
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

  void handleCommand(const std::vector<std::string>& cmd) {
    if (cmd.empty()) {
      return;
    }
    if (custom_handler_) {
      std::optional<std::string> reply = custom_handler_(cmd);
      if (reply.has_value() && client_fd_ >= 0) {
        sendRawReply(*reply);
      }
      return;
    }

    const std::string op = absl::AsciiStrToUpper(cmd[0]);
    if (op == "PING") {
      sendRawReply("+PONG\r\n");
    } else if (op == "GET" && cmd.size() == 2) {
      auto it = counters_.find(cmd[1]);
      if (it == counters_.end()) {
        sendRawReply("$-1\r\n");
      } else {
        const std::string s = absl::StrCat(it->second);
        sendRawReply(absl::StrCat("$", s.size(), "\r\n", s, "\r\n"));
      }
    } else if (op == "MGET" && cmd.size() >= 2) {
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
      if (!absl::SimpleAtoi(cmd[2], &delta)) {
        sendRawReply("-ERR value is not an integer or out of range\r\n");
        return;
      }
      const int64_t val = (counters_[cmd[1]] += delta);
      sendRawReply(absl::StrCat(":", val, "\r\n"));
    } else if (op == "EXPIRE" && cmd.size() == 3) {
      uint64_t seconds = 0;
      if (!absl::SimpleAtoi(cmd[2], &seconds)) {
        sendRawReply("-ERR value is not an integer or out of range\r\n");
        return;
      }
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
  Event::FileEventPtr listen_event_;
  Event::FileEventPtr client_event_;
  redisReader* reader_{nullptr};
  CommandHandler custom_handler_;
  absl::flat_hash_map<std::string, int64_t> counters_;
  absl::flat_hash_map<std::string, uint64_t> expirations_;
};

class AsyncRedisTest : public testing::Test {
protected:
  AsyncRedisTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        exec_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)),
        server_(*dispatcher_) {}

  template <typename T> T runOnDispatcher(Coroutine::Task<T> task) {
    std::optional<T> result;
    bool running = false;
    auto handle = Coroutine::launch(
        std::move(task), exec_,
        [this, &result, &running](T val) {
          result.emplace(std::move(val));
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
    server_.setCustomHandler([](const auto&) { return ":1\r\n"; });
    EXPECT_THAT(co_await redis->ping(), HasStatusCode(absl::StatusCode::kInternal));
    EXPECT_THAT(co_await redis->mget(std::vector<std::string>{"k"}),
                HasStatusCode(absl::StatusCode::kInternal));

    server_.setCustomHandler([](const auto&) { return "+OK\r\n"; });
    EXPECT_THAT(co_await redis->incrBy("k", 1), HasStatusCode(absl::StatusCode::kInternal));
    EXPECT_THAT(co_await redis->expire("k", 10), HasStatusCode(absl::StatusCode::kInternal));

    // `MGET` array containing an error element or non-integer string.
    server_.setCustomHandler([](const auto&) { return "*1\r\n-ERR element error\r\n"; });
    EXPECT_THAT(co_await redis->mget(std::vector<std::string>{"k"}),
                HasStatusCode(absl::StatusCode::kInternal));

    server_.setCustomHandler([](const auto&) { return "*1\r\n$3\r\nbad\r\n"; });
    EXPECT_THAT(co_await redis->mget(std::vector<std::string>{"k"}),
                HasStatusCode(absl::StatusCode::kInternal));

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

} // namespace
} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
