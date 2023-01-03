#include "source/extensions/health_checkers/cached/hiredis.h"

#include "source/common/common/assert.h"
#include "source/common/event/dispatcher_impl.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace CachedHealthChecker {

namespace {

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

Command::Command(char* data, int len) : data_(data), size_(len) {
  if (data == nullptr || len < 0) {
    throw EnvoyException("failed to format command");
  }
}

Command::Command(Command&& that) noexcept { move(std::move(that)); }

Command& Command::operator=(Command&& that) noexcept {
  if (this != &that) {
    move(std::move(that));
  }

  return *this;
}

Command::~Command() noexcept {
  if (data_ != nullptr) {
    redisFreeCommand(data_);
  }
}

const char* Command::data() const noexcept { return data_; }

int Command::size() const noexcept { return size_; }

void Command::move(Command&& that) noexcept {
  data_ = that.data_;
  size_ = that.size_;
  that.data_ = nullptr;
  that.size_ = 0;
}

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

CacheImpl::CacheImpl(ConnectionOptionsPtr opts, Event::Dispatcher& dispatcher,
                     Random::RandomGenerator& random)
    : get_connection_(opts, dispatcher, static_cast<CachePtr>(this), random, false),
      set_connection_(opts, dispatcher, static_cast<CachePtr>(this), random, true) {}

void CacheImpl::onGetCache(const std::string& hostname, const std::string& status) {
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

void CacheImpl::add(const std::string& hostname) {
  if (!host_status_map_.contains(hostname)) {
    host_status_map_.emplace(hostname, false);
  }
  getCache(hostname);
}

bool CacheImpl::get(const std::string& hostname) {
  auto itr = host_status_map_.find(hostname);
  if (itr == host_status_map_.end()) {
    host_status_map_.emplace(hostname, false);
    getCache(hostname);
    return false;
  }
  return itr->second;
}

} // namespace CachedHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
