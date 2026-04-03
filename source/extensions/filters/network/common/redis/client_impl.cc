#include "source/extensions/filters/network/common/redis/client_impl.h"

#include <algorithm>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"
#include "source/extensions/filters/network/common/redis/client.h"

#include "absl/strings/escaping.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {
namespace {
// Shared no-op callbacks for internally-issued requests that bypass redirection (e.g. ASKING).
Common::Redis::Client::DoNothingPoolCallbacks null_pool_callbacks;

// True when the HELLO 3 reply confirms RESP3 negotiation: a Map (or RESP2-fallback Array)
// containing "proto" → 3. Anything else (bare "+OK", empty array, missing/wrong proto) means
// the upstream did not acknowledge RESP3 and the connection must be torn down.
bool isHello3SuccessResponse(const Common::Redis::RespValue& value) {
  if (value.type() != Common::Redis::RespType::Map &&
      value.type() != Common::Redis::RespType::Array) {
    return false;
  }
  const auto& kv = value.asArray();
  // Map storage is flat 2N (k0,v0,k1,v1,...). Iterate by pairs.
  for (size_t i = 0; i + 1 < kv.size(); i += 2) {
    if (kv[i].type() != Common::Redis::RespType::BulkString &&
        kv[i].type() != Common::Redis::RespType::SimpleString) {
      continue;
    }
    if (kv[i].asString() == "proto" && kv[i + 1].type() == Common::Redis::RespType::Integer &&
        kv[i + 1].asInteger() == 3) {
      return true;
    }
  }
  return false;
}
} // namespace

ConfigImpl::ConfigImpl(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings&
        config)
    : op_timeout_(PROTOBUF_GET_MS_REQUIRED(config, op_timeout)),
      enable_hashtagging_(config.enable_hashtagging()),
      enable_redirection_(config.enable_redirection()),
      max_buffer_size_before_flush_(
          config.max_buffer_size_before_flush()), // This is a scalar, so default is zero.
      buffer_flush_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(
          config, buffer_flush_timeout,
          3)), // Default timeout is 3ms. If max_buffer_size_before_flush is zero, this is not used
               // as the buffer is flushed on each request immediately.
      max_upstream_unknown_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_upstream_unknown_connections, 100)),
      enable_command_stats_(config.enable_command_stats()) {
  switch (config.read_policy()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::MASTER:
    read_policy_ = ReadPolicy::Primary;
    break;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::
      PREFER_MASTER:
    read_policy_ = ReadPolicy::PreferPrimary;
    break;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::REPLICA:
    read_policy_ = ReadPolicy::Replica;
    break;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::
      PREFER_REPLICA:
    read_policy_ = ReadPolicy::PreferReplica;
    break;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::ANY:
    read_policy_ = ReadPolicy::Any;
    break;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::
      LOCAL_ZONE_AFFINITY:
    read_policy_ = ReadPolicy::LocalZoneAffinity;
    break;
  case envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::
      LOCAL_ZONE_AFFINITY_REPLICAS_AND_PRIMARY:
    read_policy_ = ReadPolicy::LocalZoneAffinityReplicasAndPrimary;
    break;
  }

  if (config.has_connection_rate_limit()) {
    connection_rate_limit_enabled_ = true;
    connection_rate_limit_per_sec_ = config.connection_rate_limit().connection_rate_limit_per_sec();
  } else {
    connection_rate_limit_enabled_ = false;
    connection_rate_limit_per_sec_ = 100;
  }
}

ClientPtr ClientImpl::create(
    Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, EncoderPtr&& encoder,
    DecoderFactory& decoder_factory, const ConfigSharedPtr& config,
    const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
    bool is_transaction_client,
    absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
    absl::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator,
    envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions::UpstreamProtocol::
        Version upstream_protocol_version,
    Stats::Counter* upstream_resp3_hello_failure) {

  auto client = std::make_unique<ClientImpl>(
      host, dispatcher, std::move(encoder), decoder_factory, config, redis_command_stats, scope,
      is_transaction_client, aws_iam_config, aws_iam_authenticator, upstream_protocol_version,
      upstream_resp3_hello_failure);
  client->connection_ = host->createConnection(dispatcher, nullptr, nullptr).connection_;
  client->connection_->addConnectionCallbacks(*client);
  client->connection_->addReadFilter(Network::ReadFilterSharedPtr{new UpstreamReadFilter(*client)});
  client->connection_->connect();
  client->connection_->noDelay(true);
  // Negotiation (AUTH/HELLO 3/READONLY/IAM) runs in initialize(), called by the factory after
  // create() returns; no wire bytes flow from here.
  return client;
}

void ClientImpl::sendAwsIamAuth(
    const std::string& auth_username,
    const envoy::extensions::filters::network::redis_proxy::v3::AwsIam& aws_iam_config) {
  ASSERT(init_state_ == InitState::WaitingForAwsToken);
  // Lifetime guard for the deferred token-fetch callback: the closure captures a weak_ptr to
  // aws_init_state_, so if *this is destroyed before the IAM token arrives the eventual fire
  // observes an expired/null parent and short-circuits without touching freed memory.
  aws_init_state_ = std::make_shared<AwsInitCallbackState>();
  aws_init_state_->parent = this;
  std::weak_ptr<AwsInitCallbackState> weak = aws_init_state_;

  // Capture aws_iam_config by value because the proto reference handed in by initialize()
  // points at aws_iam_config_ on this client and would also dangle after destruction.
  auto on_token = [weak, auth_username, aws_iam_config_copy = aws_iam_config]() {
    auto state = weak.lock();
    if (!state || state->parent == nullptr) {
      return; // ClientImpl is gone or torn down; do nothing.
    }
    state->parent->onAwsCredentialsReady(auth_username, aws_iam_config_copy);
  };

  if (!aws_iam_authenticator_.value()->addCallbackIfCredentialsPending(on_token)) {
    // Token already cached — fire synchronously.
    on_token();
  }
}

void ClientImpl::onAwsCredentialsReady(
    const std::string& auth_username,
    const envoy::extensions::filters::network::redis_proxy::v3::AwsIam& aws_iam_config) {
  if (init_state_ == InitState::Failed) {
    return; // Connection died while we were waiting for the token.
  }
  ASSERT(init_state_ == InitState::WaitingForAwsToken);
  const auto auth_password =
      aws_iam_authenticator_.value()->getAuthToken(auth_username, aws_iam_config);
  if (upstream_protocol_version_ == envoy::extensions::filters::network::redis_proxy::v3::
                                        RedisProtocolOptions::UpstreamProtocol::RESP3) {
    setInitState(InitState::AwaitingHello);
    sendResp3InitCommands(auth_username, auth_password);
  } else {
    // RESP2 + IAM: send AUTH via makeRequestInternal (NOT immediate) so the bytes flow through
    // the encoder buffer in deterministic FIFO order with any later flush. Held user requests
    // queued in held_user_requests_ are released by setInitState(Ready) → replayHeldUserRequests
    // when the AUTH ack arrives.
    setInitState(InitState::AwaitingAuth);
    Utility::AuthRequest auth_request(auth_username, auth_password);
    // Suppress makeRequestInternal's auto-flush so the threshold=0 case doesn't fire two
    // writes (one for the AUTH command + one for the empty post-flush buffer); a single
    // explicit flush after restoring queue_enabled_ keeps both threshold=0 and threshold>0
    // configurations on one wire write.
    queue_enabled_ = true;
    makeRequestInternal(auth_request, awsiam_auth_init_callbacks_);
    queue_enabled_ = false;
    flushBufferAndResetTimer();
  }
}

ClientImpl::ClientImpl(
    Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, EncoderPtr&& encoder,
    DecoderFactory& decoder_factory, const ConfigSharedPtr& config,
    const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
    bool is_transaction_client,
    absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
    absl::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator,
    envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions::UpstreamProtocol::
        Version upstream_protocol_version,
    Stats::Counter* upstream_resp3_hello_failure)
    : host_(host), encoder_(std::move(encoder)), decoder_(decoder_factory.create(*this)),
      config_(config),
      connect_or_op_timer_(dispatcher.createTimer([this]() { onConnectOrOpTimeout(); })),
      flush_timer_(dispatcher.createTimer([this]() { flushBufferAndResetTimer(); })),
      time_source_(dispatcher.timeSource()), redis_command_stats_(redis_command_stats),
      scope_(scope), is_transaction_client_(is_transaction_client), aws_iam_config_(aws_iam_config),
      aws_iam_authenticator_(aws_iam_authenticator),
      upstream_protocol_version_(upstream_protocol_version),
      upstream_resp3_hello_failure_(upstream_resp3_hello_failure), hello_init_callbacks_(*this),
      readonly_init_callbacks_(*this), awsiam_auth_init_callbacks_(*this) {

  Upstream::ClusterTrafficStats& traffic_stats = *host->cluster().trafficStats();
  traffic_stats.upstream_cx_total_.inc();
  host->stats().cx_total_.inc();
  traffic_stats.upstream_cx_active_.inc();
  host->stats().cx_active_.inc();
  connect_or_op_timer_->enableTimer(host->cluster().connectTimeout());
}

ClientImpl::~ClientImpl() {
  ASSERT(pending_requests_.empty());
  ASSERT(held_user_requests_.empty());
  ASSERT(connection_->state() == Network::Connection::State::Closed);
  // Neutralize the AWS IAM token-fetch callback in case it fires after we are gone — the closure
  // holds a weak_ptr to *aws_init_state_ and on lock() will see parent=nullptr and short-circuit.
  if (aws_init_state_) {
    aws_init_state_->parent = nullptr;
  }
  host_->cluster().trafficStats()->upstream_cx_active_.dec();
  host_->stats().cx_active_.dec();
}

void ClientImpl::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void ClientImpl::flushBufferAndResetTimer() {
  if (flush_timer_->enabled()) {
    flush_timer_->disableTimer();
  }
  connection_->write(encoder_buffer_, false);
}

PoolRequest* ClientImpl::makeRequest(const RespValue& request, ClientCallbacks& callbacks) {
  ASSERT(connection_->state() == Network::Connection::State::Open);

  // While init is in flight, hold user requests so they cannot race ahead of the in-flight
  // init command on the wire. queue_enabled_ blocks flush only, not append, so it cannot
  // substitute for this gate.
  if (isUserTrafficGated(init_state_)) {
    auto held = std::make_unique<HeldUserRequest>(*this, callbacks);
    held->request_ = std::make_unique<RespValue>(request);
    auto* raw = held.get();
    held_user_requests_.push_back(std::move(held));
    raw->self_iter_ = std::prev(held_user_requests_.end());
    return raw;
  }
  return makeRequestInternal(request, callbacks);
}

PoolRequest* ClientImpl::makeRequestInternal(const RespValue& request, ClientCallbacks& callbacks) {
  ASSERT(connection_->state() == Network::Connection::State::Open);

  const bool empty_buffer = encoder_buffer_.length() == 0;

  Stats::StatName command;
  if (config_->enableCommandStats()) {
    // Only lowercase command and get StatName if we enable command stats
    command = redis_command_stats_->getCommandFromRequest(request);
    redis_command_stats_->updateStatsTotal(scope_, command);
  } else {
    // If disabled, we use a placeholder stat name "unused" that is not used
    command = redis_command_stats_->getUnusedStatName();
  }

  pending_requests_.emplace_back(*this, callbacks, command);
  encoder_->encode(request, encoder_buffer_);

  // queue_enabled_ batches the post-init flush into one write; otherwise flush immediately
  // when the buffer hits the configured threshold, or arm the buffer-flush timer.
  if (!queue_enabled_) {
    if (encoder_buffer_.length() >= config_->maxBufferSizeBeforeFlush()) {
      flushBufferAndResetTimer();
    } else if (empty_buffer) {
      flush_timer_->enableTimer(std::chrono::milliseconds(config_->bufferFlushTimeoutInMs()));
    }
  }

  // Arm op timeout only on the first pending request after connect; before connect the
  // connect timeout governs (allowing a long TLS handshake spin-up).
  if (connected_ && pending_requests_.size() == 1) {
    connect_or_op_timer_->enableTimer(config_->opTimeout());
  }

  return &pending_requests_.back();
}

void ClientImpl::onConnectOrOpTimeout() {
  putOutlierEvent(Upstream::Outlier::Result::LocalOriginTimeout);
  if (connected_) {
    host_->cluster().trafficStats()->upstream_rq_timeout_.inc();
    host_->stats().rq_timeout_.inc();
  } else {
    host_->cluster().trafficStats()->upstream_cx_connect_timeout_.inc();
    host_->stats().cx_connect_fail_.inc();
  }

  connection_->close(Network::ConnectionCloseType::NoFlush);
}

void ClientImpl::onData(Buffer::Instance& data) {
  TRY_NEEDS_AUDIT { decoder_->decode(data); }
  END_TRY catch (ProtocolError&) {
    putOutlierEvent(Upstream::Outlier::Result::ExtOriginRequestFailed);
    host_->cluster().trafficStats()->upstream_cx_protocol_error_.inc();
    host_->stats().rq_error_.inc();
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void ClientImpl::putOutlierEvent(Upstream::Outlier::Result result) {
  if (!config_->disableOutlierEvents()) {
    host_->outlierDetector().putResult(result);
  }
}

void ClientImpl::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {

    // Transition to Failed FIRST so any init callback invoked by the pending_requests_ drain
    // sees state==Failed in onInitFailure and short-circuits the recursive close. This also
    // drains any held user requests that never made it to pending_requests_.
    if (isUserTrafficGated(init_state_)) {
      setInitState(InitState::Failed);
    }

    Upstream::reportUpstreamCxDestroy(host_, event);
    if (!pending_requests_.empty()) {
      Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
      if (event == Network::ConnectionEvent::RemoteClose) {
        putOutlierEvent(Upstream::Outlier::Result::LocalOriginConnectFailed);
      }
    }

    while (!pending_requests_.empty()) {
      PendingRequest& request = pending_requests_.front();
      if (!request.canceled_) {
        // For replayed HeldUserRequest entries this is HeldUserRequest::onFailure, which
        // forwards to original_callbacks_.onFailure() and removes self from
        // held_user_requests_. So the held queue is partially drained by this loop already.
        request.callbacks_.onFailure();
      } else {
        host_->cluster().trafficStats()->upstream_rq_cancelled_.inc();
        request.callbacks_.onCancelComplete(); // wrapper cleanup hook (default no-op)
      }
      pending_requests_.pop_front();
    }

    connect_or_op_timer_->disableTimer();
  } else if (event == Network::ConnectionEvent::Connected) {
    connected_ = true;
    // Pre-refactor this asserted pending_requests_ non-empty because initialize() always queued
    // AUTH/READONLY synchronously before connect(). With the WaitingForAwsToken path, init may
    // queue NOTHING until the IAM token arrives — so just enable the op timer when there is
    // something to time, and leave it alone otherwise.
    if (!pending_requests_.empty()) {
      connect_or_op_timer_->enableTimer(config_->opTimeout());
    }
  }

  if (event == Network::ConnectionEvent::RemoteClose && !connected_) {
    host_->cluster().trafficStats()->upstream_cx_connect_fail_.inc();
    host_->stats().cx_connect_fail_.inc();
  }
}

void ClientImpl::onRespValue(RespValuePtr&& value) {
  // RESP3 Push is server-initiated, not a reply. This PR does not route any Push-producing
  // feature (no SUBSCRIBE forwarding, no CLIENT TRACKING), so a well-behaved upstream should
  // not send Push frames on our connections in steady state. Drop unexpected Pushes rather
  // than pop a pending request — popping would corrupt pending_requests_ FIFO ordering against
  // the next genuine reply. Silent drop preserves the FIFO invariant and lets future
  // Push-routing work add real handling without touching this hot path.
  if (value && value->type() == RespType::Push) {
    ENVOY_LOG(debug, "redis: dropping unexpected upstream RESP3 Push frame");
    return;
  }
  ASSERT(!pending_requests_.empty());
  PendingRequest& request = pending_requests_.front();
  const bool canceled = request.canceled_;

  if (config_->enableCommandStats()) {
    bool success = !canceled && (value->type() != Common::Redis::RespType::Error) &&
                   (value->type() != Common::Redis::RespType::BlobError);
    redis_command_stats_->updateStats(scope_, request.command_, success);
    request.command_request_timer_->complete();
  }
  request.aggregate_request_timer_->complete();

  ClientCallbacks& callbacks = request.callbacks_;

  // We need to ensure the request is popped before calling the callback, since the callback might
  // result in closing the connection.
  pending_requests_.pop_front();
  if (canceled) {
    host_->cluster().trafficStats()->upstream_rq_cancelled_.inc();
    // Wrapper callbacks (e.g. HeldUserRequest) may need to release themselves now that the live
    // PendingRequest that referenced them is gone. Default base impl is a no-op.
    callbacks.onCancelComplete();
  } else if (config_->enableRedirection() && !is_transaction_client_ &&
             (value->type() == Common::Redis::RespType::Error ||
              value->type() == Common::Redis::RespType::BlobError)) {
    std::vector<absl::string_view> err = StringUtil::splitToken(value->asString(), " ", false);
    if (err.size() == 3 &&
        (err[0] == RedirectionResponse::get().MOVED || err[0] == RedirectionResponse::get().ASK)) {
      // MOVED and ASK redirection errors have the following substrings: MOVED or ASK (err[0]), hash
      // key slot (err[1]), and IP address and TCP port separated by a colon (err[2])
      callbacks.onRedirection(std::move(value), std::string(err[2]),
                              err[0] == RedirectionResponse::get().ASK);
    } else {
      // splitToken with keep_empty_string=false returns an empty vector for
      // an empty/whitespace-only error string. Guard before err[0] — an
      // empty Error/BlobError reply ("-\r\n" / "!0\r\n\r\n") would otherwise
      // dereference past the end of the vector.
      if (!err.empty() && err[0] == RedirectionResponse::get().CLUSTER_DOWN) {
        callbacks.onFailure();
      } else {
        callbacks.onResponse(std::move(value));
      }
    }
  } else {
    callbacks.onResponse(std::move(value));
  }

  // If there are no remaining ops in the pipeline we need to disable the timer.
  // Otherwise we boost the timer since we are receiving responses and there are more to flush
  // out.
  if (pending_requests_.empty()) {
    connect_or_op_timer_->disableTimer();
  } else {
    connect_or_op_timer_->enableTimer(config_->opTimeout());
  }

  putOutlierEvent(Upstream::Outlier::Result::ExtOriginRequestSuccess);
}

ClientImpl::PendingRequest::PendingRequest(ClientImpl& parent, ClientCallbacks& callbacks,
                                           Stats::StatName command)
    : parent_(parent), callbacks_(callbacks), command_{command},
      aggregate_request_timer_(parent_.redis_command_stats_->createAggregateTimer(
          parent_.scope_, parent_.time_source_)) {
  if (parent_.config_->enableCommandStats()) {
    command_request_timer_ = parent_.redis_command_stats_->createCommandTimer(
        parent_.scope_, command_, parent_.time_source_);
  }
  parent.host_->cluster().trafficStats()->upstream_rq_total_.inc();
  parent.host_->stats().rq_total_.inc();
  parent.host_->cluster().trafficStats()->upstream_rq_active_.inc();
  parent.host_->stats().rq_active_.inc();
}

ClientImpl::PendingRequest::~PendingRequest() {
  parent_.host_->cluster().trafficStats()->upstream_rq_active_.dec();
  parent_.host_->stats().rq_active_.dec();
}

void ClientImpl::PendingRequest::cancel() {
  // If we get a cancellation, we just mark the pending request as cancelled, and then we drop
  // the response as it comes through. There is no reason to blow away the connection when the
  // remote is already responding as fast as possible.
  canceled_ = true;
}

void ClientImpl::initialize(const std::string& auth_username, const std::string& auth_password) {
  using ProtoVersion =
      envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions::UpstreamProtocol;

  // AWS IAM cases are routed first because they need to defer all init dispatch until the IAM
  // token arrives. The state transition to WaitingForAwsToken is what makes the held-user-queue
  // gate kick in for any user request the conn pool dispatches between this return and the
  // token's eventual arrival.
  if (aws_iam_authenticator_.has_value() && aws_iam_config_.has_value()) {
    setInitState(InitState::WaitingForAwsToken);
    sendAwsIamAuth(auth_username, *aws_iam_config_);
    return;
  }

  if (upstream_protocol_version_ != ProtoVersion::RESP3) {
    // RESP2 + no IAM — legacy synchronous-init semantics: initialize() runs inside
    // ClientFactoryImpl::create() so user makeRequest cannot interleave between these calls and
    // the factory return. State stays NotStarted across the makeRequestInternal calls so the
    // gate (isUserTrafficGated) does not fire on these init commands; we then snap to Ready.
    if (!auth_username.empty()) {
      Utility::AuthRequest auth_request(auth_username, auth_password);
      makeRequestInternal(auth_request, null_pool_callbacks);
    } else if (!auth_password.empty()) {
      Utility::AuthRequest auth_request(auth_password);
      makeRequestInternal(auth_request, null_pool_callbacks);
    }
    if (config_->readPolicy() != Common::Redis::Client::ReadPolicy::Primary) {
      makeRequestInternal(Utility::ReadOnlyRequest::instance(), null_pool_callbacks);
    }
    setInitState(InitState::Ready);
    return;
  }

  // RESP3 + no IAM. Engage the held-queue gate so any user makeRequest the pool dispatches after
  // this returns is parked until HELLO (and READONLY, when applicable) succeed.
  setInitState(InitState::AwaitingHello);
  sendResp3InitCommands(auth_username, auth_password);
}

void ClientImpl::sendResp3InitCommands(const std::string& auth_username,
                                       const std::string& auth_password) {
  std::vector<std::string> hello_args = {"3"};
  // Mirror RESP2 AUTH semantics: send credentials when EITHER username or password is set.
  // Username-only is a valid Redis 6 ACL configuration (an ACL user with no password); RESP2
  // sends bare AUTH for it, RESP3 must do the same via HELLO 3 AUTH.
  if (!auth_username.empty() || !auth_password.empty()) {
    hello_args.push_back("AUTH");
    // Redis 6 ACL synonym: AUTH-with-just-password is equivalent to AUTH default <pass>.
    hello_args.push_back(auth_username.empty() ? "default" : auth_username);
    hello_args.push_back(auth_password);
  }
  auto hello = Utility::makeRequest("HELLO", hello_args);
  // Suppress makeRequestInternal's auto-flush; one explicit flush below pushes HELLO as a
  // single wire write across both threshold=0 and threshold>0 configurations. READONLY is
  // NOT sent yet — strict phases: if HELLO fails we never want READONLY on an unnegotiated
  // connection.
  queue_enabled_ = true;
  makeRequestInternal(hello, hello_init_callbacks_);
  queue_enabled_ = false;
  flushBufferAndResetTimer();
}

void ClientImpl::sendReadonlyInit() {
  ASSERT(init_state_ == InitState::AwaitingReadonly);
  queue_enabled_ = true;
  makeRequestInternal(Utility::ReadOnlyRequest::instance(), readonly_init_callbacks_);
  queue_enabled_ = false;
  flushBufferAndResetTimer();
}

void ClientImpl::onInitStepSuccess(InitState completed_step) {
  if (init_state_ == InitState::Failed) {
    return; // already torn down by an earlier step's failure
  }
  ASSERT(init_state_ == completed_step);
  switch (completed_step) {
  case InitState::AwaitingHello:
  case InitState::AwaitingAuth:
    // Both HELLO 3 (RESP3 path) and post-IAM AUTH (RESP2 + IAM path) finish the credentials
    // step; READONLY is the next phase whenever the read policy may target replicas. Skipping
    // it on RESP2+IAM would leave non-Primary reads pinned to the master and silently diverge
    // from RESP2-no-IAM and RESP3 behavior.
    if (config_->readPolicy() != Common::Redis::Client::ReadPolicy::Primary) {
      setInitState(InitState::AwaitingReadonly);
      sendReadonlyInit();
    } else {
      setInitState(InitState::Ready);
    }
    break;
  case InitState::AwaitingReadonly:
    setInitState(InitState::Ready);
    break;
  default:
    IS_ENVOY_BUG("unexpected init step success");
  }
}

void ClientImpl::onInitFailure() {
  if (init_state_ == InitState::Failed) {
    return; // idempotent — second-arriving init reply, or onEvent retry
  }
  setInitState(InitState::Failed); // drains non-replayed held requests via failHeldUserRequests
  // Only close if not already mid-teardown — onInitFailure can be re-entered via onEvent's
  // pending_requests_ drain, and a second close would double-fire close-time stats.
  if (connection_->state() == Network::Connection::State::Open) {
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void ClientImpl::setInitState(InitState new_state) {
  ENVOY_LOG(debug, "redis client: init state {} -> {}", static_cast<int>(init_state_),
            static_cast<int>(new_state));
  init_state_ = new_state;
  if (new_state == InitState::Ready) {
    replayHeldUserRequests();
  } else if (new_state == InitState::Failed) {
    failHeldUserRequests();
  }
}

void ClientImpl::replayHeldUserRequests() {
  if (held_user_requests_.empty()) {
    return;
  }
  // Batch all replayed entries behind queue_enabled_ for a single flush at the end. Pre-
  // replay cancel erases entries eagerly (see HeldUserRequest::cancel), so every entry here
  // is live; replayed entries self-clean via the HeldUserRequest callbacks.
  queue_enabled_ = true;
  for (auto& held_ptr : held_user_requests_) {
    ASSERT(!held_ptr->canceled_);
    auto* live = static_cast<PendingRequest*>(makeRequestInternal(*held_ptr->request_, *held_ptr));
    held_ptr->live_request_ = live;
  }
  queue_enabled_ = false;
  flushBufferAndResetTimer();
}

void ClientImpl::failHeldUserRequests() {
  // Drain non-replayed entries (live_request_ == nullptr); erase before firing the callback so
  // re-entrant mutations (sibling cancel, recursive setInitState(Failed)) are safe. Replayed
  // entries are owned by pending_requests_ and fail through onEvent's drain instead.
  while (true) {
    auto it = std::find_if(
        held_user_requests_.begin(), held_user_requests_.end(),
        [](const std::unique_ptr<HeldUserRequest>& h) { return h->live_request_ == nullptr; });
    if (it == held_user_requests_.end()) {
      return;
    }
    auto held = std::move(*it);
    held_user_requests_.erase(it);
    if (!held->canceled_) {
      held->original_callbacks_.onFailure();
    }
    // Canceled non-replayed: stat already incremented in HeldUserRequest::cancel(); no fire.
    // held destroyed here.
  }
}

void ClientImpl::removeHeldUserRequest(HeldUserRequest* held) {
  // O(1) erase via stored iterator. unique_ptr destruction here destroys *held; the caller must
  // not access *held after this returns.
  held_user_requests_.erase(held->self_iter_);
}

void ClientImpl::HeldUserRequest::cancel() {
  if (canceled_) {
    return;
  }
  canceled_ = true;
  if (live_request_ != nullptr) {
    // Forward to the live PendingRequest so the cancel stat + canceled-branch fire through
    // onRespValue (matching non-held cancel semantics). Do NOT self-destruct here: the live
    // PendingRequest's callbacks_ still refers to *this, and onRespValue / onEvent bind that
    // reference before checking PendingRequest::canceled_. onCancelComplete is the cleanup
    // hook that erases the wrapper after that binding is gone.
    live_request_->cancel();
    return;
  }
  // Pre-replay: no PendingRequest holds a reference; erase immediately. The cancel stat
  // increment lives here because no canceled-branch fires for a request that never reached
  // pending_requests_.
  parent_.host_->cluster().trafficStats()->upstream_rq_cancelled_.inc();
  parent_.removeHeldUserRequest(this); // self-destructs; do NOT touch *this after.
}

void ClientImpl::HeldUserRequest::onCancelComplete() {
  // Called by ClientImpl::onRespValue (response arrived for a canceled request) or
  // ClientImpl::onEvent (connection closed with a canceled request still pending). At this
  // point the live PendingRequest has been popped, so no other reference to *this remains.
  parent_.removeHeldUserRequest(this); // self-destructs; do NOT touch *this after.
}

void ClientImpl::HeldUserRequest::onResponse(Common::Redis::RespValuePtr&& value) {
  if (!canceled_) {
    original_callbacks_.onResponse(std::move(value));
  }
  parent_.removeHeldUserRequest(this); // self-destructs; do NOT touch *this after.
}
void ClientImpl::HeldUserRequest::onFailure() {
  if (!canceled_) {
    original_callbacks_.onFailure();
  }
  parent_.removeHeldUserRequest(this);
}
void ClientImpl::HeldUserRequest::onRedirection(Common::Redis::RespValuePtr&& value,
                                                const std::string& host_address,
                                                bool ask_redirection) {
  if (!canceled_) {
    original_callbacks_.onRedirection(std::move(value), host_address, ask_redirection);
  }
  parent_.removeHeldUserRequest(this);
}

void ClientImpl::Hello3InitCallbacks::onResponse(Common::Redis::RespValuePtr&& value) {
  const bool is_error = value && (value->type() == Common::Redis::RespType::Error ||
                                  value->type() == Common::Redis::RespType::BlobError);
  if (is_error || !value || !isHello3SuccessResponse(*value)) {
    if (parent_.upstream_resp3_hello_failure_ != nullptr) {
      parent_.upstream_resp3_hello_failure_->inc();
    }
    if (is_error) {
      ENVOY_LOG(warn, "redis: HELLO 3 negotiation failed: {}", absl::CHexEscape(value->asString()));
    } else {
      ENVOY_LOG(warn, "redis: HELLO 3 negotiation failed: unexpected reply shape "
                      "(expected Map containing proto=3)");
    }
    parent_.onInitFailure();
    return;
  }
  parent_.onInitStepSuccess(InitState::AwaitingHello);
}
void ClientImpl::Hello3InitCallbacks::onFailure() {
  if (parent_.upstream_resp3_hello_failure_ != nullptr) {
    parent_.upstream_resp3_hello_failure_->inc();
  }
  ENVOY_LOG(warn, "redis: HELLO 3 negotiation failed (connection error)");
  parent_.onInitFailure();
}
void ClientImpl::Hello3InitCallbacks::onRedirection(Common::Redis::RespValuePtr&& value,
                                                    const std::string&, bool ask_redirection) {
  // HELLO does not honor redirection — Redis never returns MOVED/ASK for it. Treat as failure and
  // tear down so the next user request lands on a fresh connection that will retry the handshake.
  if (parent_.upstream_resp3_hello_failure_ != nullptr) {
    parent_.upstream_resp3_hello_failure_->inc();
  }
  ENVOY_LOG(warn, "redis: HELLO 3 received {} redirection (treating as failure): {}",
            ask_redirection ? "ASK" : "MOVED", value ? absl::CHexEscape(value->asString()) : "");
  parent_.onInitFailure();
}

void ClientImpl::ReadOnlyInitCallbacks::onResponse(Common::Redis::RespValuePtr&& value) {
  const bool is_error = value && (value->type() == Common::Redis::RespType::Error ||
                                  value->type() == Common::Redis::RespType::BlobError);
  if (is_error) {
    ENVOY_LOG(warn, "redis: READONLY init failed: {}", absl::CHexEscape(value->asString()));
    parent_.onInitFailure();
    return;
  }
  parent_.onInitStepSuccess(InitState::AwaitingReadonly);
}
void ClientImpl::ReadOnlyInitCallbacks::onFailure() {
  ENVOY_LOG(warn, "redis: READONLY init failed (connection error)");
  parent_.onInitFailure();
}
void ClientImpl::ReadOnlyInitCallbacks::onRedirection(Common::Redis::RespValuePtr&&,
                                                      const std::string&, bool) {
  // READONLY does not honor redirection. Same reasoning as HELLO.
  parent_.onInitFailure();
}

void ClientImpl::AwsIamAuthInitCallbacks::onResponse(Common::Redis::RespValuePtr&& value) {
  const bool is_error = value && (value->type() == Common::Redis::RespType::Error ||
                                  value->type() == Common::Redis::RespType::BlobError);
  if (is_error) {
    ENVOY_LOG(warn, "redis: AWS IAM AUTH init failed: {}", absl::CHexEscape(value->asString()));
    parent_.onInitFailure();
    return;
  }
  parent_.onInitStepSuccess(InitState::AwaitingAuth);
}
void ClientImpl::AwsIamAuthInitCallbacks::onFailure() {
  ENVOY_LOG(warn, "redis: AWS IAM AUTH init failed (connection error)");
  parent_.onInitFailure();
}
void ClientImpl::AwsIamAuthInitCallbacks::onRedirection(Common::Redis::RespValuePtr&&,
                                                        const std::string&, bool) {
  parent_.onInitFailure();
}

ClientFactoryImpl ClientFactoryImpl::instance_;

ClientPtr ClientFactoryImpl::create(
    Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, const ConfigSharedPtr& config,
    const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
    const std::string& auth_username, const std::string& auth_password, bool is_transaction_client,
    absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
    absl::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator,
    envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions::UpstreamProtocol::
        Version upstream_protocol_version,
    Stats::Counter* upstream_resp3_hello_failure) {

  ClientPtr client = ClientImpl::create(
      host, dispatcher, EncoderPtr{new EncoderImpl()}, decoder_factory_, config,
      redis_command_stats, scope, is_transaction_client, aws_iam_config, aws_iam_authenticator,
      upstream_protocol_version, upstream_resp3_hello_failure);
  // initialize() routes internally by upstream protocol version and IAM presence — it is the
  // single entry point that drives RESP2/RESP3 + with/without AWS IAM through the same state
  // machine, so the factory calls it unconditionally for every client.
  client->initialize(auth_username, auth_password);
  return client;
}

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
