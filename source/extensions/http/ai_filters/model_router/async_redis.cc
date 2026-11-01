#include "source/extensions/http/ai_filters/model_router/async_redis.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"

#include "source/common/common/assert.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/status_macros.h"

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "hiredis/async.h"
#include "hiredis/hiredis.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

namespace {

absl::Status parsePingReply(const redisReply& reply) {
  if ((reply.type == REDIS_REPLY_STATUS || reply.type == REDIS_REPLY_STRING) &&
      absl::string_view(reply.str, reply.len) == "PONG") {
    return absl::OkStatus();
  }
  return absl::InternalError("Unexpected reply to PING");
}

absl::StatusOr<int64_t> parseCounterReply(const redisReply& reply) {
  if (reply.type == REDIS_REPLY_NIL) {
    return 0;
  }
  if (reply.type == REDIS_REPLY_INTEGER) {
    return static_cast<int64_t>(reply.integer);
  }
  if (reply.type == REDIS_REPLY_STRING) {
    int64_t value = 0;
    const absl::string_view str(reply.str, reply.len);
    if (!absl::SimpleAtoi(str, &value)) {
      return absl::InternalError(absl::StrCat("Failed to parse Redis counter value: ", str));
    }
    return value;
  }
  return absl::InternalError(absl::StrCat("Unexpected Redis reply type for counter: ", reply.type));
}

absl::StatusOr<std::vector<int64_t>> parseMgetReply(const redisReply& reply) {
  if (reply.type != REDIS_REPLY_ARRAY) {
    return absl::InternalError(absl::StrCat("Unexpected Redis reply type for MGET: ", reply.type));
  }
  std::vector<int64_t> values;
  values.reserve(reply.elements);
  for (size_t i = 0; i < reply.elements; ++i) {
    if (reply.element[i] == nullptr) {
      return absl::InternalError("Null element in MGET reply");
    }
    if (reply.element[i]->type == REDIS_REPLY_ERROR) {
      return absl::InternalError(absl::string_view(reply.element[i]->str, reply.element[i]->len));
    }
    absl::StatusOr<int64_t> val = parseCounterReply(*reply.element[i]);
    if (!val.ok()) {
      return val.status();
    }
    values.push_back(*val);
  }
  return values;
}

absl::StatusOr<int64_t> parseIncrByReply(const redisReply& reply) {
  if (reply.type == REDIS_REPLY_INTEGER) {
    return static_cast<int64_t>(reply.integer);
  }
  return absl::InternalError(absl::StrCat("Unexpected Redis reply type for INCRBY: ", reply.type));
}

absl::Status parseExpireReply(const redisReply& reply) {
  if (reply.type == REDIS_REPLY_INTEGER) {
    return absl::OkStatus();
  }
  return absl::InternalError(absl::StrCat("Unexpected Redis reply type for EXPIRE: ", reply.type));
}

class ConnectAwaitable : public Coroutine::LeafAwaitable<absl::Status> {
public:
  explicit ConnectAwaitable(redisAsyncContext* context) : context_(context) {}

protected:
  std::optional<absl::Status> tryImmediate() override {
    if (context_ == nullptr) {
      return absl::UnavailableError("Redis context is closed");
    }
    if ((context_->c.flags & REDIS_CONNECTED) != 0) {
      return absl::OkStatus();
    }
    if (context_->data != nullptr) {
      return absl::FailedPreconditionError("Redis operation is already in progress");
    }
    return std::nullopt;
  }

  void onStart() override {
    context_->data = this;
    if (context_->onConnect == nullptr &&
        redisAsyncSetConnectCallback(context_, &ConnectAwaitable::onConnect) != REDIS_OK) {
      context_->data = nullptr;
      complete(absl::UnavailableError("Failed to register Redis connect callback"));
    }
  }

  void onCancel() override {
    if (context_ != nullptr && context_->data == this) {
      context_->data = nullptr;
    }
  }

private:
  static void onConnect(const redisAsyncContext* context, int status) {
    auto* mut_context = const_cast<redisAsyncContext*>(context);
    auto* self = static_cast<ConnectAwaitable*>(std::exchange(mut_context->data, nullptr));
    if (status != REDIS_OK && mut_context->ev.cleanup != nullptr) {
      // `hiredis` calls `__redisAsyncDisconnect` immediately after a failed `onConnect`;
      // run cleanup now so `AsyncRedis::context_` is cleared before the coroutine resumes.
      auto* cleanup = std::exchange(mut_context->ev.cleanup, nullptr);
      cleanup(mut_context->ev.data);
    }
    if (self == nullptr) {
      return;
    }
    if (status == REDIS_OK) {
      self->complete(absl::OkStatus());
    } else {
      const char* err_msg = (context->errstr != nullptr && context->errstr[0] != '\0')
                                ? context->errstr
                                : "Failed to connect to Redis";
      self->complete(absl::UnavailableError(err_msg));
    }
  }

  redisAsyncContext* context_;
};

class CloseAwaitable : public Coroutine::LeafAwaitable<absl::Status> {
public:
  explicit CloseAwaitable(redisAsyncContext* context) : context_(context) {}

protected:
  std::optional<absl::Status> tryImmediate() override {
    if (context_ == nullptr) {
      return absl::OkStatus();
    }
    if ((context_->c.flags & REDIS_CONNECTED) == 0) {
      if (context_->ev.cleanup != nullptr) {
        auto* cleanup = std::exchange(context_->ev.cleanup, nullptr);
        cleanup(context_->ev.data);
      }
      context_->onConnect = nullptr;
      context_->onDisconnect = nullptr;
      context_->data = nullptr;
      redisAsyncFree(context_);
      context_ = nullptr;
      return absl::OkStatus();
    }
    if (context_->data != nullptr) {
      return absl::FailedPreconditionError("Redis close is already in progress");
    }
    return std::nullopt;
  }

  void onStart() override {
    context_->data = this;
    redisAsyncSetDisconnectCallback(context_, &CloseAwaitable::onDisconnect);
    redisAsyncDisconnect(context_);
  }

  void onCancel() override {
    if (context_ != nullptr && context_->data == this) {
      context_->data = nullptr;
    }
  }

private:
  static void onDisconnect(const redisAsyncContext* context, int status) {
    auto* mut_context = const_cast<redisAsyncContext*>(context);
    auto* self = static_cast<CloseAwaitable*>(std::exchange(mut_context->data, nullptr));
    if (self == nullptr) {
      return;
    }
    self->context_ = nullptr;
    if (status == REDIS_OK) {
      self->complete(absl::OkStatus());
    } else {
      const char* err_msg = (context->errstr != nullptr && context->errstr[0] != '\0')
                                ? context->errstr
                                : "Redis disconnected with error";
      self->complete(absl::UnavailableError(err_msg));
    }
  }

  redisAsyncContext* context_;
};

template <typename T> class CommandAwaitable : public Coroutine::LeafAwaitable<T> {
public:
  using ParserFn = T (*)(const redisReply&);

  CommandAwaitable(redisAsyncContext* context, std::vector<std::string> args, ParserFn parser)
      : context_(context), args_(std::move(args)), parser_(parser) {}

protected:
  std::optional<T> tryImmediate() override {
    if (context_ == nullptr || (context_->c.flags & REDIS_CONNECTED) == 0 ||
        (context_->c.flags & (REDIS_DISCONNECTING | REDIS_FREEING)) != 0) {
      return absl::UnavailableError("Redis client is not connected");
    }
    return std::nullopt;
  }

  void onStart() override {
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    argv.reserve(args_.size());
    argvlen.reserve(args_.size());
    for (const std::string& arg : args_) {
      argv.push_back(arg.data());
      argvlen.push_back(arg.size());
    }

    state_ = new CallbackState{this};
    const int rc =
        redisAsyncCommandArgv(context_, &CommandAwaitable<T>::onReply, state_,
                              static_cast<int>(argv.size()), argv.data(), argvlen.data());
    if (rc != REDIS_OK) {
      delete std::exchange(state_, nullptr);
      const char* err_msg = (context_->errstr != nullptr && context_->errstr[0] != '\0')
                                ? context_->errstr
                                : "Failed to issue Redis command";
      this->complete(absl::UnavailableError(err_msg));
    }
  }

  void onCancel() override {
    if (state_ != nullptr) {
      // Do not delete `state_` here: `hiredis` still holds it as `privdata` in its reply
      // queue and will invoke `onReply` (either on reply arrival or context teardown),
      // which deletes `state`.
      state_->awaitable = nullptr;
      state_ = nullptr;
    }
  }

private:
  // Heap-allocated indirection so a cancelled `CommandAwaitable` can be destroyed while
  // `hiredis` still holds `privdata` in its reply queue until `onReply` runs.
  struct CallbackState {
    CommandAwaitable<T>* awaitable;
  };

  static void onReply(redisAsyncContext* ac, void* raw_reply, void* privdata) {
    auto* state = static_cast<CallbackState*>(privdata);
    CommandAwaitable<T>* self = state->awaitable;
    delete state;
    if (self == nullptr) {
      // The coroutine was cancelled.
      return;
    }
    self->state_ = nullptr;

    if (raw_reply == nullptr) {
      const char* err_msg = (ac != nullptr && ac->errstr != nullptr && ac->errstr[0] != '\0')
                                ? ac->errstr
                                : "Redis connection closed while awaiting reply";
      self->complete(absl::UnavailableError(err_msg));
      return;
    }

    const auto* reply = static_cast<const redisReply*>(raw_reply);
    if (reply->type == REDIS_REPLY_ERROR) {
      self->complete(absl::InternalError(absl::string_view(reply->str, reply->len)));
      return;
    }

    self->complete(self->parser_(*reply));
  }

  redisAsyncContext* context_;
  std::vector<std::string> args_;
  ParserFn parser_;
  CallbackState* state_{nullptr};
};

} // namespace

absl::StatusOr<std::unique_ptr<AsyncRedis>> AsyncRedis::open(Event::Dispatcher& dispatcher,
                                                             const std::string& host, int port,
                                                             std::chrono::milliseconds timeout) {
  redisOptions options{};
  REDIS_OPTIONS_SET_TCP(&options, host.c_str(), port);
  timeval tv{};
  if (timeout.count() > 0) {
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    options.connect_timeout = &tv;
    options.command_timeout = &tv;
  }

  redisAsyncContext* context = redisAsyncConnectWithOptions(&options);
  if (context == nullptr) {
    return absl::UnavailableError("Failed to allocate Redis async context");
  }
  if (context->err) {
    absl::Status error = absl::UnavailableError(context->errstr);
    redisAsyncFree(context);
    return error;
  }

  return absl::WrapUnique(new AsyncRedis(context, dispatcher));
}

AsyncRedis::AsyncRedis(redisAsyncContext* context, Event::Dispatcher& dispatcher)
    : context_(context), dispatcher_(dispatcher) {
  context_->ev.data = this;
  context_->ev.addRead = &AsyncRedis::onAddRead;
  context_->ev.delRead = &AsyncRedis::onDelRead;
  context_->ev.addWrite = &AsyncRedis::onAddWrite;
  context_->ev.delWrite = &AsyncRedis::onDelWrite;
  context_->ev.cleanup = &AsyncRedis::onCleanup;
  context_->ev.scheduleTimer = &AsyncRedis::onScheduleTimer;

  read_event_ = dispatcher_.createFileEvent(
      context_->c.fd,
      [this](uint32_t) {
        redisAsyncHandleRead(context_);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Level, 0);

  write_event_ = dispatcher_.createFileEvent(
      context_->c.fd,
      [this](uint32_t) {
        redisAsyncHandleWrite(context_);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Level, 0);

  timer_ = dispatcher_.createTimer([this]() { redisAsyncHandleTimeout(context_); });
}

AsyncRedis::~AsyncRedis() {
  read_event_.reset();
  write_event_.reset();
  timer_.reset();
  if (context_ != nullptr) {
    redisAsyncContext* ctx = std::exchange(context_, nullptr);
    ctx->ev.addRead = nullptr;
    ctx->ev.delRead = nullptr;
    ctx->ev.addWrite = nullptr;
    ctx->ev.delWrite = nullptr;
    ctx->ev.cleanup = nullptr;
    ctx->ev.scheduleTimer = nullptr;
    ctx->ev.data = nullptr;
    ctx->onConnect = nullptr;
    ctx->onDisconnect = nullptr;
    ctx->data = nullptr;
    redisAsyncFree(ctx);
  }
}

void AsyncRedis::onAddRead(void* privdata) {
  auto* self = static_cast<AsyncRedis*>(privdata);
  ASSERT(self != nullptr && self->read_event_ != nullptr);
  self->read_event_->setEnabled(Event::FileReadyType::Read);
}

void AsyncRedis::onDelRead(void* privdata) {
  auto* self = static_cast<AsyncRedis*>(privdata);
  ASSERT(self != nullptr && self->read_event_ != nullptr);
  self->read_event_->setEnabled(0);
}

void AsyncRedis::onAddWrite(void* privdata) {
  auto* self = static_cast<AsyncRedis*>(privdata);
  ASSERT(self != nullptr && self->write_event_ != nullptr);
  self->write_event_->setEnabled(Event::FileReadyType::Write);
}

void AsyncRedis::onDelWrite(void* privdata) {
  auto* self = static_cast<AsyncRedis*>(privdata);
  ASSERT(self != nullptr && self->write_event_ != nullptr);
  self->write_event_->setEnabled(0);
}

void AsyncRedis::onCleanup(void* privdata) {
  auto* self = static_cast<AsyncRedis*>(privdata);
  ASSERT(self != nullptr);
  self->context_ = nullptr;
  self->read_event_.reset();
  self->write_event_.reset();
  self->timer_.reset();
}

void AsyncRedis::onScheduleTimer(void* privdata, timeval tv) {
  auto* self = static_cast<AsyncRedis*>(privdata);
  ASSERT(self != nullptr && self->timer_ != nullptr);
  const auto duration =
      std::chrono::seconds(tv.tv_sec) +
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::microseconds(tv.tv_usec));
  self->timer_->enableTimer(duration);
}

Coroutine::Task<absl::Status> AsyncRedis::connect() {
  co_return co_await ConnectAwaitable(context_);
}

Coroutine::Task<absl::Status> AsyncRedis::close() { co_return co_await CloseAwaitable(context_); }

Coroutine::Task<absl::Status> AsyncRedis::ping() {
  co_return co_await CommandAwaitable<absl::Status>(context_, {"PING"}, &parsePingReply);
}

Coroutine::Task<absl::StatusOr<int64_t>> AsyncRedis::get(absl::string_view key) {
  co_return co_await CommandAwaitable<absl::StatusOr<int64_t>>(context_, {"GET", std::string(key)},
                                                               &parseCounterReply);
}

Coroutine::Task<absl::StatusOr<std::vector<int64_t>>>
AsyncRedis::mget(absl::Span<const std::string> keys) {
  if (keys.empty()) {
    co_return std::vector<int64_t>{};
  }
  std::vector<std::string> args;
  args.reserve(keys.size() + 1);
  args.emplace_back("MGET");
  for (const std::string& key : keys) {
    args.push_back(key);
  }
  co_return co_await CommandAwaitable<absl::StatusOr<std::vector<int64_t>>>(
      context_, std::move(args), &parseMgetReply);
}

Coroutine::Task<absl::StatusOr<int64_t>> AsyncRedis::incrBy(absl::string_view key, int64_t delta) {
  co_return co_await CommandAwaitable<absl::StatusOr<int64_t>>(
      context_, {"INCRBY", std::string(key), absl::StrCat(delta)}, &parseIncrByReply);
}

Coroutine::Task<absl::Status> AsyncRedis::expire(absl::string_view key, uint64_t seconds) {
  co_return co_await CommandAwaitable<absl::Status>(
      context_, {"EXPIRE", std::string(key), absl::StrCat(seconds)}, &parseExpireReply);
}

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
