#include "extensions/health_checkers/redis/redis.h"

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/health_checker/redis/v2/redis.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

RedisHealthChecker::RedisHealthChecker(
    const Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
    const envoy::config::health_checker::redis::v2::Redis& redis_config,
    Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Random::RandomGenerator& random,
    Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api,
    Extensions::NetworkFilters::Common::Redis::Client::ClientFactory& client_factory)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random, std::move(event_logger)),
      client_factory_(client_factory), key_(redis_config.key()),
      auth_username_(
          NetworkFilters::RedisProxy::ProtocolOptionsConfigImpl::authUsername(cluster.info(), api)),
      auth_password_(NetworkFilters::RedisProxy::ProtocolOptionsConfigImpl::authPassword(
          cluster.info(), api)) {
  if (!key_.empty()) {
    type_ = Type::Exists;
  } else {
    type_ = Type::Ping;
  }
}

RedisHealthChecker::RedisActiveHealthCheckSession::RedisActiveHealthCheckSession(
    RedisHealthChecker& parent, const Upstream::HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent) {
  redis_command_stats_ =
      Extensions::NetworkFilters::Common::Redis::RedisCommandStats::createRedisCommandStats(
          parent_.cluster_.info()->statsScope().symbolTable());
}

RedisHealthChecker::RedisActiveHealthCheckSession::~RedisActiveHealthCheckSession() {
  ASSERT(current_request_ == nullptr);
  ASSERT(client_ == nullptr);
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onDeferredDelete() {
  if (current_request_) {
    current_request_->cancel();
    current_request_ = nullptr;
  }

  if (client_) {
    client_->close();
  }
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // This should only happen after any active requests have been failed/cancelled.
    ASSERT(!current_request_);
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onInterval() {
  if (!client_) {
    client_ = parent_.client_factory_.create(
        host_, parent_.dispatcher_, *this, redis_command_stats_,
        parent_.cluster_.info()->statsScope(), parent_.auth_username_, parent_.auth_password_);
    client_->addConnectionCallbacks(*this);
  }

  ASSERT(!current_request_);

  switch (parent_.type_) {
  case Type::Exists:
    current_request_ = client_->makeRequest(existsHealthCheckRequest(parent_.key_), *this);
    break;
  case Type::Ping:
    current_request_ = client_->makeRequest(pingHealthCheckRequest(), *this);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onResponse(
    NetworkFilters::Common::Redis::RespValuePtr&& value) {
  current_request_ = nullptr;

  switch (parent_.type_) {
  case Type::Exists:
    if (value->type() == NetworkFilters::Common::Redis::RespType::Integer &&
        value->asInteger() == 0) {
      handleSuccess();
    } else {
      handleFailure(envoy::data::core::v3::ACTIVE);
    }
    break;
  case Type::Ping:
    if (value->type() == NetworkFilters::Common::Redis::RespType::SimpleString &&
        value->asString() == "PONG") {
      handleSuccess();
    } else {
      handleFailure(envoy::data::core::v3::ACTIVE);
    }
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  if (!parent_.reuse_connection_) {
    client_->close();
  }
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onFailure() {
  current_request_ = nullptr;
  handleFailure(envoy::data::core::v3::NETWORK);
}

bool RedisHealthChecker::RedisActiveHealthCheckSession::onRedirection(
    NetworkFilters::Common::Redis::RespValuePtr&&, const std::string&, bool) {
  // Treat any redirection error response from a Redis server as success.
  current_request_ = nullptr;
  handleSuccess();
  return true;
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onTimeout() {
  current_request_->cancel();
  current_request_ = nullptr;
  client_->close();
}

RedisHealthChecker::HealthCheckRequest::HealthCheckRequest(const std::string& key) {
  std::vector<NetworkFilters::Common::Redis::RespValue> values(2);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "EXISTS";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = key;
  request_.type(NetworkFilters::Common::Redis::RespType::Array);
  request_.asArray().swap(values);
}

RedisHealthChecker::HealthCheckRequest::HealthCheckRequest() {
  std::vector<NetworkFilters::Common::Redis::RespValue> values(1);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "PING";
  request_.type(NetworkFilters::Common::Redis::RespType::Array);
  request_.asArray().swap(values);
}

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
