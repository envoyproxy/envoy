#include "extensions/health_checkers/redis/redis.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

RedisHealthChecker::RedisHealthChecker(
    const Upstream::Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
    const envoy::config::health_checker::redis::v2::Redis& redis_config,
    Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    Extensions::NetworkFilters::RedisProxy::ConnPool::ClientFactory& client_factory)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random),
      client_factory_(client_factory), key_(redis_config.key()) {
  if (!key_.empty()) {
    type_ = Type::Exists;
  } else {
    type_ = Type::Ping;
  }
}

RedisHealthChecker::RedisActiveHealthCheckSession::RedisActiveHealthCheckSession(
    RedisHealthChecker& parent, const Upstream::HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent) {}

RedisHealthChecker::RedisActiveHealthCheckSession::~RedisActiveHealthCheckSession() {
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
    client_ = parent_.client_factory_.create(host_, parent_.dispatcher_, *this);
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
    NOT_REACHED;
  }
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onResponse(
    Extensions::NetworkFilters::RedisProxy::RespValuePtr&& value) {
  current_request_ = nullptr;

  switch (parent_.type_) {
  case Type::Exists:
    if (value->type() == Extensions::NetworkFilters::RedisProxy::RespType::Integer &&
        value->asInteger() == 0) {
      handleSuccess();
    } else {
      handleFailure(FailureType::Active);
    }
    break;
  case Type::Ping:
    if (value->type() == Extensions::NetworkFilters::RedisProxy::RespType::SimpleString &&
        value->asString() == "PONG") {
      handleSuccess();
    } else {
      handleFailure(FailureType::Active);
    }
    break;
  default:
    NOT_REACHED;
  }

  if (!parent_.reuse_connection_) {
    client_->close();
  }
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onFailure() {
  current_request_ = nullptr;
  handleFailure(FailureType::Network);
}

void RedisHealthChecker::RedisActiveHealthCheckSession::onTimeout() {
  current_request_->cancel();
  current_request_ = nullptr;
  client_->close();
}

RedisHealthChecker::HealthCheckRequest::HealthCheckRequest(const std::string& key) {
  std::vector<Extensions::NetworkFilters::RedisProxy::RespValue> values(2);
  values[0].type(Extensions::NetworkFilters::RedisProxy::RespType::BulkString);
  values[0].asString() = "EXISTS";
  values[1].type(Extensions::NetworkFilters::RedisProxy::RespType::BulkString);
  values[1].asString() = key;
  request_.type(Extensions::NetworkFilters::RedisProxy::RespType::Array);
  request_.asArray().swap(values);
}

RedisHealthChecker::HealthCheckRequest::HealthCheckRequest() {
  std::vector<Extensions::NetworkFilters::RedisProxy::RespValue> values(1);
  values[0].type(Extensions::NetworkFilters::RedisProxy::RespType::BulkString);
  values[0].asString() = "PING";
  request_.type(Extensions::NetworkFilters::RedisProxy::RespType::Array);
  request_.asArray().swap(values);
}

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy