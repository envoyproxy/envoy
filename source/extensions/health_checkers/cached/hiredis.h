#pragma once

#include <hiredis/adapters/libevent.h>
#include <hiredis/hiredis.h>
#include <hiredis/hiredis_ssl.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/random_generator.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/c_smart_ptr.h"
#include "source/common/config/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/health_checkers/cached/client.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

namespace {

constexpr char IS_HEALTHY[] = "IsHealthy";

constexpr char DEFAULT_PATTERN[] = "__keyevent@%u__:set";

constexpr uint32_t RetryInitialDelayMs = 257;

constexpr uint32_t RetryMaxDelayMs = 32257;

static inline bool isError(redisReply& reply) { return reply.type == REDIS_REPLY_ERROR; }

static inline void throwError(const redisContext& ctx, const std::string& err_info) {
  auto err_code = ctx.err;
  const auto* err_str = ctx.errstr;
  if (err_str == nullptr) {
    throw EnvoyException(err_info + static_cast<std::string>(": null error message: ") +
                         std::to_string(err_code));
  } else {
    auto err_msg = err_info + static_cast<std::string>(": ") + err_str;
    throw EnvoyException(err_msg);
  }
}

static inline void throwError(const redisReply& reply) {
  ASSERT(reply.type == REDIS_REPLY_ERROR);

  if (reply.str == nullptr) {
    throw EnvoyException("Null error reply");
  }

  auto err_str = std::string(reply.str, reply.len);

  throw EnvoyException(err_str);
}

static inline timeval toTimeval(const std::chrono::milliseconds& dur) {
  auto sec = std::chrono::duration_cast<std::chrono::seconds>(dur);
  auto msec = std::chrono::duration_cast<std::chrono::microseconds>(dur - sec);

  timeval t;
  t.tv_sec = sec.count();
  t.tv_usec = msec.count();
  return t;
}

} // namespace

class Command {
public:
  Command(char* data, int len) : data_(data), size_(len) {
    if (data == nullptr || len < 0) {
      throw EnvoyException("failed to format command");
    }
  }

  Command(const Command&) = delete;
  Command& operator=(const Command&) = delete;

  Command(Command&& that) noexcept { move(std::move(that)); }

  Command& operator=(Command&& that) noexcept {
    if (this != &that) {
      move(std::move(that));
    }

    return *this;
  }

  ~Command() noexcept {
    if (data_ != nullptr) {
      redisFreeCommand(data_);
    }
  }

  const char* data() const noexcept { return data_; }

  int size() const noexcept { return size_; }

private:
  void move(Command&& that) noexcept {
    data_ = that.data_;
    size_ = that.size_;
    that.data_ = nullptr;
    that.size_ = 0;
  }

  char* data_ = nullptr;
  int size_ = 0;
};

template <typename... Args> Command cmd(const char* format, Args&&... args) {
  char* data = nullptr;
  auto len = redisFormatCommand(&data, format, std::forward<Args>(args)...);

  return Command(data, len);
}

template <typename Callback> class Handler {
public:
  Handler(Callback c) : c_(c) {}

  static void callback(redisAsyncContext* ctx, void* reply, void* privdata) {
    (static_cast<Handler<Callback>*>(privdata))->operator()(ctx, reply);
  }

  void operator()(redisAsyncContext* ctx, void* reply) {
    c_(static_cast<redisReply*>(reply));
    redisContext* c = &(ctx->c);
    if (c->flags & REDIS_SUBSCRIBED)
      return;
    delete (this);
  }

private:
  Callback c_;
};

class BaseEvent {
public:
  virtual ~BaseEvent() = default;

  virtual void handle(redisAsyncContext* ctx) = 0;
};

using BaseEventUPtr = std::unique_ptr<BaseEvent>;

template <typename Callback> class CommandEvent : public BaseEvent {
public:
  explicit CommandEvent(Command cmd, Handler<Callback>* cb)
      : cmd_(std::move(cmd)), cb_(std::move(cb)) {}

  void handle(redisAsyncContext* ctx) override {
    if (redisAsyncFormattedCommand(ctx, Handler<Callback>::callback, cb_, cmd_.data(),
                                   cmd_.size()) != REDIS_OK) {
      throwError(ctx->c, "failed to send command");
    }
  }

private:
  Command cmd_;
  Handler<Callback>* cb_;
};

template <typename Callback> using CommandEventUPtr = std::unique_ptr<CommandEvent<Callback>>;

using CTlsContextPtr = CSmartPtr<redisSSLContext, redisFreeSSLContext>;

using CAsyncContextPtr = CSmartPtr<redisAsyncContext, redisAsyncFree>;

class Connection : public Logger::Loggable<Logger::Id::hc> {
public:
  Connection(ConnectionOptionsPtr opts, Event::Dispatcher& dispatcher, CachePtr cache,
             Random::RandomGenerator& random, bool subscribed);

  ~Connection();

  template <typename Callback> void execCommand(Command cmd, Callback callback);

  void psubscribe(std::string pattern);

  void get(std::string hostname);

private:
  bool needAuth() const;

  bool needSelectDb() const;

  void connectingCallback();

  void authingCallback();

  void selectDbCallback();

  void auth();

  void selectDb();

  void setConnected();

  bool isDisconnected() const noexcept;

  void fail(const EnvoyException* /*err*/);

  static void setOptionsCallback(redisAsyncContext* ctx, void* r, void*);

  void eventCallback();

  void connectCallback(const EnvoyException* err);

  void disconnect(const EnvoyException* err);

  void disableDisconnectCallback();

  void send(BaseEventUPtr event);

  void send();

  std::vector<BaseEventUPtr> getEvents();

  CTlsContextPtr secureConnection(redisContext& ctx, const TlsOptions& opts);

  ConnectionOptions& options();

  void connect();

  void watch(redisAsyncContext& ctx);

  static void connected(const redisAsyncContext* ctx, int status);

  static void disconnected(const redisAsyncContext* ctx, int status);

  void disconnectCallback(const EnvoyException* err);

  CAsyncContextPtr connect(const ConnectionOptions& opts);

  void setRetryTimer();

  enum class State {
    DISCONNECTED = 0,
    CONNECTING,
    AUTHING,
    SELECTING_DB,
    CONNECTED,
  };

  std::atomic<State> state_{State::DISCONNECTED};

  ConnectionOptionsPtr opts_;
  Event::Dispatcher& dispatcher_;
  CachePtr cache_;
  bool subscribed_ = false;
  struct event_base* base_;
  Event::SchedulableCallbackPtr event_callback_;

  CTlsContextPtr tls_ctx_;
  redisAsyncContext* ctx_ = nullptr;
  std::vector<std::unique_ptr<BaseEvent>> events_;
  std::mutex mtx_;
  bool run_disconnect_callback_ = true;

  BackOffStrategyPtr backoff_strategy_;
  Event::TimerPtr retry_timer_;
  Random::RandomGenerator& random_;
};

class CacheImpl : public Cache, public Logger::Loggable<Logger::Id::hc> {
public:
  CacheImpl(ConnectionOptionsPtr opts, Event::Dispatcher& dispatcher,
            Random::RandomGenerator& random)
      : get_connection_(opts, dispatcher, static_cast<CachePtr>(this), random, false),
        set_connection_(opts, dispatcher, static_cast<CachePtr>(this), random, true) {}

  void onSetCache(const std::string& hostname) override { getCache(hostname); }

  void onGetCache(const std::string& hostname, const std::string& status) override {
    if (!host_status_map_.contains(hostname)) {
      return;
    }
    try {
      const std::string status_document_value = std::string(status);
      Json::ObjectSharedPtr document_json;
      document_json = Json::Factory::loadFromString(status_document_value);
      const bool is_healthy = document_json->getBoolean(IS_HEALTHY);
      host_status_map_.insert_or_assign(hostname, is_healthy);
    } catch (EnvoyException& e) {
      ENVOY_LOG(error,
                "CachedHealthChecker Cache onGetCache. Could not parse health status for host "
                "'{}': {}",
                hostname, e.what());
    }
  }

  void add(const std::string& hostname) override {
    if (!host_status_map_.contains(hostname)) {
      host_status_map_.emplace(hostname, false);
    }
    getCache(hostname);
  }

  void remove(const std::string& hostname) override { host_status_map_.erase(hostname); }

  bool get(const std::string& hostname) override {
    auto itr = host_status_map_.find(hostname);
    if (itr == host_status_map_.end()) {
      host_status_map_.emplace(hostname, false);
      getCache(hostname);
      return false;
    }
    return itr->second;
  }

private:
  void getCache(const std::string& hostname) { get_connection_.get(hostname); }

  Connection get_connection_;
  Connection set_connection_;

  absl::node_hash_map<std::string, bool> host_status_map_;
};

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
