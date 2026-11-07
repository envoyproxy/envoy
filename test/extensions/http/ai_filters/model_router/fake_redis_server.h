#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"

#include "source/common/common/assert.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "hiredis/hiredis.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

template <typename T>
T runTaskOnDispatcher(Event::Dispatcher& dispatcher,
                      const std::shared_ptr<Coroutine::Executor>& executor,
                      Coroutine::Task<T> task) {
  std::optional<T> result;
  bool running = false;
  auto handle = Coroutine::launch(
      std::move(task), executor,
      [&dispatcher, &result, &running](T value) {
        result.emplace(std::move(value));
        if (running) {
          dispatcher.exit();
        }
      },
      Coroutine::StartMode::Inline);
  if (!result.has_value()) {
    running = true;
    dispatcher.run(Event::Dispatcher::RunType::RunUntilExit);
    running = false;
  }
  RELEASE_ASSERT(result.has_value(), "Task did not complete");
  return std::move(*result);
}

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
  }

  int port() const { return port_; }
  size_t acceptedConnections() const { return accepted_connections_; }
  const absl::flat_hash_map<std::string, uint64_t>& expirations() const { return expirations_; }

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
  size_t accepted_connections_{0};
  Event::FileEventPtr listen_event_;
  Event::FileEventPtr client_event_;
  redisReader* reader_{nullptr};
  CommandHandler custom_handler_;
  absl::flat_hash_map<std::string, int64_t> counters_;
  absl::flat_hash_map<std::string, uint64_t> expirations_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
