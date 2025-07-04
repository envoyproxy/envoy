#include "source/extensions/filters/network/common/redis/client_impl.h"

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"

#include "client.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {
namespace {
// null_pool_callbacks is used for requests that must be filtered and not redirected such as
// "asking".
Common::Redis::Client::DoNothingPoolCallbacks null_pool_callbacks;

// Custom authentication callback handler for AWS IAM authentication
class AuthCallbackHandler : public DoNothingPoolCallbacks {
  void onResponse(Common::Redis::RespValuePtr&& resp) override {
    ENVOY_LOG_MISC(debug, "AWS IAM Authentication Response: {}", resp->toString());
  }
};

AuthCallbackHandler auth_callbacks;
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
    bool is_transaction_client, const std::string& auth_username,
    absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
    absl::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator) {

  auto client = std::make_unique<ClientImpl>(
      host, dispatcher, std::move(encoder), decoder_factory, config, redis_command_stats, scope,
      is_transaction_client, aws_iam_config, aws_iam_authenticator);
  client->connection_ = host->createConnection(dispatcher, nullptr, nullptr).connection_;
  client->connection_->addConnectionCallbacks(*client);
  client->connection_->addReadFilter(Network::ReadFilterSharedPtr{new UpstreamReadFilter(*client)});
  client->connection_->connect();
  client->connection_->noDelay(true);

  // The presence of a valid auth_username is checked during filter initialization
  if (aws_iam_authenticator.has_value() && aws_iam_config.has_value()) {
    client->sendAwsIamAuth(auth_username, aws_iam_config.value());
  }

  return client;
}

void ClientImpl::sendAwsIamAuth(
    const std::string& auth_username,
    const envoy::extensions::filters::network::redis_proxy::v3::AwsIam& aws_iam_config) {
  queueRequests(true);
  auto add_auth = [this, auth_username, &aws_iam_config]() {
    const auto auth_password =
        aws_iam_authenticator_.value()->getAuthToken(auth_username, aws_iam_config);
    Envoy::Extensions::NetworkFilters::Common::Redis::Utility::AuthRequest auth_request(
        auth_username, auth_password);
    makeRequestImmediate(auth_request, auth_callbacks);
    queueRequests(false);
  };

  if (aws_iam_authenticator_.value()->addCallbackIfCredentialsPending(
          [add_auth]() { add_auth(); }) == false) {
    add_auth();
  }
}

ClientImpl::ClientImpl(
    Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, EncoderPtr&& encoder,
    DecoderFactory& decoder_factory, const ConfigSharedPtr& config,
    const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
    bool is_transaction_client,
    absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
    absl::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator)
    : host_(host), encoder_(std::move(encoder)), decoder_(decoder_factory.create(*this)),
      config_(config),
      connect_or_op_timer_(dispatcher.createTimer([this]() { onConnectOrOpTimeout(); })),
      flush_timer_(dispatcher.createTimer([this]() { flushBufferAndResetTimer(); })),
      time_source_(dispatcher.timeSource()), redis_command_stats_(redis_command_stats),
      scope_(scope), is_transaction_client_(is_transaction_client), aws_iam_config_(aws_iam_config),
      aws_iam_authenticator_(aws_iam_authenticator) {

  Upstream::ClusterTrafficStats& traffic_stats = *host->cluster().trafficStats();
  traffic_stats.upstream_cx_total_.inc();
  host->stats().cx_total_.inc();
  traffic_stats.upstream_cx_active_.inc();
  host->stats().cx_active_.inc();
  connect_or_op_timer_->enableTimer(host->cluster().connectTimeout());
}

ClientImpl::~ClientImpl() {
  ASSERT(pending_requests_.empty());
  ASSERT(connection_->state() == Network::Connection::State::Closed);
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

  // If we have enabled queuing (to pause AUTH while credentials are being used), don't flush our
  // buffers
  if (!queue_enabled_) {
    // If buffer is full, flush. If the buffer was empty before the request, start the timer.
    if (encoder_buffer_.length() >= config_->maxBufferSizeBeforeFlush()) {
      flushBufferAndResetTimer();
    } else if (empty_buffer) {
      flush_timer_->enableTimer(std::chrono::milliseconds(config_->bufferFlushTimeoutInMs()));
    }
  }

  // Only boost the op timeout if:
  // - We are not already connected. Otherwise, we are governed by the connect timeout and the timer
  //   will be reset when/if connection occurs. This allows a relatively long connection spin up
  //   time for example if TLS is being used.
  // - This is the first request on the pipeline. Otherwise the timeout would effectively start on
  //   the last operation.
  if (connected_ && pending_requests_.size() == 1) {
    connect_or_op_timer_->enableTimer(config_->opTimeout());
  }

  return &pending_requests_.back();
}

PoolRequest* ClientImpl::makeRequestImmediate(const RespValue& request,
                                              ClientCallbacks& callbacks) {
  ASSERT(connection_->state() == Network::Connection::State::Open);

  Stats::StatName command;
  if (config_->enableCommandStats()) {
    // Only lowercase command and get StatName if we enable command stats
    command = redis_command_stats_->getCommandFromRequest(request);
    redis_command_stats_->updateStatsTotal(scope_, command);
  } else {
    // If disabled, we use a placeholder stat name "unused" that is not used
    command = redis_command_stats_->getUnusedStatName();
  }
  Buffer::OwnedImpl immediate_buffer;
  pending_requests_.emplace_back(*this, callbacks, command);
  encoder_->encode(request, immediate_buffer);
  connection_->write(immediate_buffer, false);
  // Flush buffer if we've queued up any requests while waiting for authentication credentials
  if (encoder_buffer_.length()) {
    flushBufferAndResetTimer();
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
        request.callbacks_.onFailure();
      } else {
        host_->cluster().trafficStats()->upstream_rq_cancelled_.inc();
      }
      pending_requests_.pop_front();
    }

    connect_or_op_timer_->disableTimer();
  } else if (event == Network::ConnectionEvent::Connected) {
    connected_ = true;
    ASSERT(!pending_requests_.empty());
    connect_or_op_timer_->enableTimer(config_->opTimeout());
  }

  if (event == Network::ConnectionEvent::RemoteClose && !connected_) {
    host_->cluster().trafficStats()->upstream_cx_connect_fail_.inc();
    host_->stats().cx_connect_fail_.inc();
  }
}

void ClientImpl::onRespValue(RespValuePtr&& value) {
  ASSERT(!pending_requests_.empty());
  PendingRequest& request = pending_requests_.front();
  const bool canceled = request.canceled_;

  if (config_->enableCommandStats()) {
    bool success = !canceled && (value->type() != Common::Redis::RespType::Error);
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
  } else if (config_->enableRedirection() && !is_transaction_client_ &&
             (value->type() == Common::Redis::RespType::Error)) {
    std::vector<absl::string_view> err = StringUtil::splitToken(value->asString(), " ", false);
    if (err.size() == 3 &&
        (err[0] == RedirectionResponse::get().MOVED || err[0] == RedirectionResponse::get().ASK)) {
      // MOVED and ASK redirection errors have the following substrings: MOVED or ASK (err[0]), hash
      // key slot (err[1]), and IP address and TCP port separated by a colon (err[2])
      callbacks.onRedirection(std::move(value), std::string(err[2]),
                              err[0] == RedirectionResponse::get().ASK);
    } else {
      if (err[0] == RedirectionResponse::get().CLUSTER_DOWN) {
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
  if (!auth_username.empty()) {
    // Send an AUTH command to the upstream server with username and password.
    Utility::AuthRequest auth_request(auth_username, auth_password);
    makeRequest(auth_request, null_pool_callbacks);
  } else if (!auth_password.empty()) {
    // Send an AUTH command to the upstream server.
    Utility::AuthRequest auth_request(auth_password);
    makeRequest(auth_request, null_pool_callbacks);
  }
  // Any connection to replica requires the READONLY command in order to perform read.
  // Also the READONLY command is a no-opt for the primary.
  // We only need to send the READONLY command iff it's possible that the host is a replica.
  if (config_->readPolicy() != Common::Redis::Client::ReadPolicy::Primary) {
    makeRequest(Utility::ReadOnlyRequest::instance(), null_pool_callbacks);
  }
}

ClientFactoryImpl ClientFactoryImpl::instance_;

ClientPtr ClientFactoryImpl::create(
    Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, const ConfigSharedPtr& config,
    const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
    const std::string& auth_username, const std::string& auth_password, bool is_transaction_client,
    absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
    absl::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
        aws_iam_authenticator) {

  ClientPtr client =
      ClientImpl::create(host, dispatcher, EncoderPtr{new EncoderImpl()}, decoder_factory_, config,
                         redis_command_stats, scope, is_transaction_client, auth_username,
                         aws_iam_config, aws_iam_authenticator);

  if (!aws_iam_authenticator.has_value()) {
    client->initialize(auth_username, auth_password);
  }
  return client;
}

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
