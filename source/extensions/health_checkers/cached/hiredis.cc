#include "source/common/common/assert.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/extensions/health_checkers/cached/hiredis.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

Connection::Connection(ConnectionOptionsPtr opts, Event::Dispatcher& dispatcher, CachePtr cache,
                       Random::RandomGenerator& random, bool subscribed)
    : opts_(opts), dispatcher_(dispatcher), cache_(cache), subscribed_(subscribed),
      base_(&static_cast<Event::DispatcherImpl*>(&dispatcher_)->base()),
      event_callback_(dispatcher_.createSchedulableCallback([this]() { eventCallback(); })),
      random_(random) {
  event_callback_->scheduleCallbackNextIteration();
  backoff_strategy_ = std::make_unique<JitteredExponentialBackOffStrategy>(
      RetryInitialDelayMs, RetryMaxDelayMs, random_);
  retry_timer_ = dispatcher_.createTimer([this]() -> void { connect(); });

  connect();
}

Connection::~Connection() {
  if (event_callback_ != nullptr && event_callback_->enabled()) {
    event_callback_->cancel();
  }
  if (retry_timer_ && retry_timer_->enabled()) {
    retry_timer_->disableTimer();
  }
}

template <typename Callback> void Connection::execCommand(Command cmd, Callback callback) {
  auto event = CommandEventUPtr<Callback>(
      new CommandEvent<Callback>(std::move(cmd), new Handler<Callback>(callback)));
  send(std::move(event));
}

void Connection::psubscribe(std::string pattern) {
  auto lambdaOnPmsg = [&](redisReply* reply) {
    if (reply && reply->elements == 4) {
      if (reply->element[0] && reply->element[0]->str &&
          !strncasecmp(reply->element[0]->str, "pmessage", reply->element[0]->len) &&
          reply->element[3] && reply->element[3]->str) {
        auto hostname = std::string(reply->element[3]->str);
        if (cache_)
          cache_->onSetCache(hostname);
      }
    }
  };
  execCommand(cmd("PSUBSCRIBE %b", pattern.data(), pattern.size()), lambdaOnPmsg);
}

void Connection::get(std::string hostname) {
  auto lambdaOnResult = [&, hostname](redisReply* reply) {
    if (reply && reply->str) {
      auto status = std::string(reply->str);
      if (cache_)
        cache_->onGetCache(hostname, status);
    };
  };
  execCommand(cmd("GET %b", hostname.data(), hostname.size()), lambdaOnResult);
}

bool Connection::needAuth() const { return !opts_->password.empty() || opts_->user != "default"; }

bool Connection::needSelectDb() const { return opts_->db != 0; }

void Connection::connectingCallback() {
  if (needAuth()) {
    auth();
  } else if (needSelectDb()) {
    selectDb();
  } else {
    setConnected();
  }
}

void Connection::authingCallback() {
  if (needSelectDb()) {
    selectDb();
  } else {
    setConnected();
  }
}

void Connection::selectDbCallback() { setConnected(); }

void Connection::auth() {
  ASSERT(!isDisconnected());

  if (opts_->user == "default") {
    if (redisAsyncCommand(ctx_, setOptionsCallback, nullptr, "AUTH %b", opts_->password.data(),
                          opts_->password.size()) != REDIS_OK) {
      throw EnvoyException("failed to send auth command");
    }
  } else {
    // Redis 6.0 or latter
    if (redisAsyncCommand(ctx_, setOptionsCallback, nullptr, "AUTH %b %b", opts_->user.data(),
                          opts_->user.size(), opts_->password.data(),
                          opts_->password.size()) != REDIS_OK) {
      throw EnvoyException("failed to send auth command");
    }
  }

  state_ = State::AUTHING;
}

void Connection::selectDb() {
  ASSERT(!isDisconnected());

  if (redisAsyncCommand(ctx_, setOptionsCallback, nullptr, "SELECT %d", opts_->db) != REDIS_OK) {
    throw EnvoyException("failed to send select command");
  }

  state_ = State::SELECTING_DB;
}

void Connection::setConnected() {
  state_ = State::CONNECTED;

  if (subscribed_) {
    std::string pattern = absl::StrFormat(DEFAULT_PATTERN, opts_->db);
    psubscribe(pattern);
  }

  // Send pending commands.
  send();
}

bool Connection::isDisconnected() const noexcept { return state_ == State::DISCONNECTED; }

void Connection::fail(const EnvoyException* /*err*/) {
  ctx_ = nullptr;

  state_ = State::DISCONNECTED;

  setRetryTimer();
}

void Connection::setOptionsCallback(redisAsyncContext* ctx, void* r, void*) {
  ASSERT(ctx != nullptr);

  auto* connection = static_cast<Connection*>(ctx->data);
  ASSERT(connection != nullptr);

  redisReply* reply = static_cast<redisReply*>(r);
  if (reply == nullptr) {
    // Connection has bee closed.
    return;
  }

  try {
    if (isError(*reply)) {
      throwError(*reply);
    }

  } catch (const EnvoyException& e) {
    connection->disconnect(&e);

    return;
  }

  connection->connectCallback(nullptr);
}

void Connection::eventCallback() {
  switch (state_.load()) {
  case State::CONNECTED:
    send();
    break;

  default:
    break;
  }

  if (event_callback_ != nullptr) {
    event_callback_->scheduleCallbackNextIteration();
  }
}

void Connection::connectCallback(const EnvoyException* err) {
  if (err) {
    // Failed to connect to Redis
    fail(err);

    return;
  }

  // Connect OK.
  try {
    switch (state_.load()) {
    case State::CONNECTING:
      connectingCallback();
      break;

    case State::AUTHING:
      authingCallback();
      break;

    case State::SELECTING_DB:
      selectDbCallback();
      break;

    default:
      setConnected();
    }
  } catch (const EnvoyException& e) {
    disconnect(&e);
  }
}

void Connection::disconnect(const EnvoyException* err) {
  if (ctx_ != nullptr) {
    disableDisconnectCallback();
    redisAsyncDisconnect(ctx_);
  }

  fail(err);
}

void Connection::disableDisconnectCallback() {
  ASSERT(ctx_ != nullptr);

  auto* connection = static_cast<Connection*>(ctx_->data);

  ASSERT(connection != nullptr);

  connection->run_disconnect_callback_ = false;
}

void Connection::send(BaseEventUPtr event) {
  {
    std::lock_guard<std::mutex> lock(mtx_);

    events_.push_back(std::move(event));
  }
}

void Connection::send() {
  auto events = getEvents();
  for (auto idx = 0U; idx != events.size(); ++idx) {
    auto& event = events[idx];
    try {
      event->handle(ctx_);
    } catch (const EnvoyException& e) {
      // Failed to send command, fail subsequent events.
      disconnect(&e);

      break;
    }
  }
}

std::vector<BaseEventUPtr> Connection::getEvents() {
  std::vector<BaseEventUPtr> events;
  {
    std::lock_guard<std::mutex> lock(mtx_);

    events.swap(events_);
  }

  return events;
}

CTlsContextPtr Connection::secureConnection(redisContext& ctx, const TlsOptions& opts) {
  redisInitOpenSSL();
  auto c_str = [](const std::string& s) { return s.empty() ? nullptr : s.c_str(); };

  redisSSLContextError err;
  auto tls_ctx =
      CTlsContextPtr(redisCreateSSLContext(c_str(opts.cacert), c_str(opts.capath), c_str(opts.cert),
                                           c_str(opts.key), c_str(opts.sni), &err));
  if (!tls_ctx) {
    throw EnvoyException(std::string("failed to create TLS context: ") +
                         redisSSLContextGetError(err));
  }

  if (redisInitiateSSLWithContext(&ctx, tls_ctx.get()) != REDIS_OK) {
    throwError(ctx, "Failed to initialize TLS connection");
  }

  return tls_ctx;
}

ConnectionOptions& Connection::options() {
  std::lock_guard<std::mutex> lock(mtx_);

  return *opts_;
}

void Connection::connect() {
  if (!isDisconnected()) {
    return;
  }
  try {
    auto opts = options();

    auto ctx = connect(opts);

    ASSERT(ctx && ctx->err == REDIS_OK);

    const auto& tls_opts = opts.tls;
    CTlsContextPtr tls_ctx;
    if (tls_opts.enabled) {
      tls_ctx = secureConnection(ctx->c, tls_opts);
    }

    watch(*ctx);

    tls_ctx_ = std::move(tls_ctx);
    ctx_ = ctx.release();

    state_ = State::CONNECTING;
  } catch (const EnvoyException& e) {
    fail(&e);
  }
}

void Connection::watch(redisAsyncContext& ctx) {
  if (redisLibeventAttach(&ctx, base_) != REDIS_OK) {
    throw EnvoyException("failed to attach to event loop");
  }

  redisAsyncSetConnectCallback(&ctx, connected);
  redisAsyncSetDisconnectCallback(&ctx, disconnected);
}

void Connection::connected(const redisAsyncContext* ctx, int status) {
  ASSERT(ctx != nullptr);

  auto* connection = static_cast<Connection*>(ctx->data);
  ASSERT(connection != nullptr);

  const EnvoyException* err = nullptr;
  if (status != REDIS_OK) {
    try {
      throwError(ctx->c, "failed to connect to server");
    } catch (const EnvoyException& e) {
      err = &e;
    }
  }

  connection->connectCallback(err);
}

void Connection::disconnected(const redisAsyncContext* ctx, int status) {
  ASSERT(ctx != nullptr);

  auto* connection = static_cast<Connection*>(ctx->data);
  ASSERT(connection != nullptr);

  if (!connection->run_disconnect_callback_) {
    return;
  }

  const EnvoyException* err = nullptr;
  if (status != REDIS_OK) {
    try {
      throwError(ctx->c, "failed to disconnect from server");
    } catch (const EnvoyException& e) {
      err = &e;
    }
  }

  connection->disconnectCallback(err);
}

void Connection::disconnectCallback(const EnvoyException* err) { fail(err); }

CAsyncContextPtr Connection::connect(const ConnectionOptions& opts) {
  redisOptions redis_opts;
  std::memset(&redis_opts, 0, sizeof(redis_opts));

  timeval connect_timeout;
  if (opts.connect_timeout > std::chrono::milliseconds(0)) {
    connect_timeout = toTimeval(opts.connect_timeout);
    redis_opts.connect_timeout = &connect_timeout;
  }
  timeval command_timeout;
  if (opts.command_timeout > std::chrono::milliseconds(0)) {
    command_timeout = toTimeval(opts.command_timeout);
    redis_opts.command_timeout = &command_timeout;
  }

  redis_opts.type = REDIS_CONN_TCP;
  redis_opts.endpoint.tcp.ip = opts.host.c_str();
  redis_opts.endpoint.tcp.port = opts.port;

  auto* context = redisAsyncConnectWithOptions(&redis_opts);
  if (context == nullptr) {
    throw EnvoyException("Failed to allocate memory for connection.");
  }

  auto ctx = CAsyncContextPtr(context);
  if (ctx->err != REDIS_OK) {
    throwError(ctx->c, "failed to connect to server");
  }

  ctx->data = static_cast<void*>(this);

  return ctx;
}

void Connection::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
}

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
