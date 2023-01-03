#pragma once

#include <hiredis/adapters/libevent.h>
#include <hiredis/hiredis.h>
#include <hiredis/hiredis_ssl.h>

#include <atomic>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/random_generator.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/c_smart_ptr.h"
#include "source/common/common/thread.h"
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

} // namespace

class Command {
public:
  Command(char* data, int len);

  Command(const Command&) = delete;
  Command& operator=(const Command&) = delete;

  Command(Command&& that) noexcept;

  Command& operator=(Command&& that) noexcept;

  ~Command() noexcept;

  const char* data() const noexcept;

  int size() const noexcept;

private:
  void move(Command&& that) noexcept;

  char* data_ = nullptr;
  int size_ = 0;
};

class BaseEvent {
public:
  virtual ~BaseEvent() = default;

  virtual void handle(redisAsyncContext* ctx) = 0;
};

using BaseEventUPtr = std::unique_ptr<BaseEvent>;

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
  Thread::MutexBasicLockable events_lock_;
  Thread::MutexBasicLockable opts_lock_;
  bool run_disconnect_callback_ = true;

  BackOffStrategyPtr backoff_strategy_;
  Event::TimerPtr retry_timer_;
  Random::RandomGenerator& random_;
};

class CacheImpl : public Cache, public Logger::Loggable<Logger::Id::hc> {
public:
  CacheImpl(ConnectionOptionsPtr opts, Event::Dispatcher& dispatcher,
            Random::RandomGenerator& random);

  void onSetCache(const std::string& hostname) override { getCache(hostname); }

  void onGetCache(const std::string& hostname, const std::string& status) override;

  void add(const std::string& hostname) override;

  void remove(const std::string& hostname) override { host_status_map_.erase(hostname); }

  bool get(const std::string& hostname) override;

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
