#include "common/upstream/health_checker_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/hex.h"
#include "common/common/utility.h"
#include "common/http/codec_client.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/redis/conn_pool_impl.h"
#include "common/upstream/host_utility.h"

namespace Envoy {
namespace Upstream {

HealthCheckerPtr HealthCheckerFactory::create(const Json::Object& hc_config,
                                              Upstream::Cluster& cluster, Runtime::Loader& runtime,
                                              Runtime::RandomGenerator& random,
                                              Event::Dispatcher& dispatcher) {
  hc_config.validateSchema(Json::Schema::CLUSTER_HEALTH_CHECK_SCHEMA);

  const std::string hc_type = hc_config.getString("type");
  if (hc_type == "http") {
    return HealthCheckerPtr{
        new ProdHttpHealthCheckerImpl(cluster, hc_config, dispatcher, runtime, random)};
  } else if (hc_type == "tcp") {
    return HealthCheckerPtr{
        new TcpHealthCheckerImpl(cluster, hc_config, dispatcher, runtime, random)};
  } else {
    ASSERT(hc_type == "redis");
    return HealthCheckerPtr{
        new RedisHealthCheckerImpl(cluster, hc_config, dispatcher, runtime, random,
                                   Redis::ConnPool::ClientFactoryImpl::instance_)};
  }
}

const std::chrono::milliseconds HealthCheckerImplBase::NO_TRAFFIC_INTERVAL{60000};

HealthCheckerImplBase::HealthCheckerImplBase(const Cluster& cluster, const Json::Object& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random)
    : cluster_(cluster), dispatcher_(dispatcher), timeout_(config.getInteger("timeout_ms")),
      unhealthy_threshold_(config.getInteger("unhealthy_threshold")),
      healthy_threshold_(config.getInteger("healthy_threshold")),
      stats_(generateStats(cluster.info()->statsScope())), runtime_(runtime), random_(random),
      interval_(config.getInteger("interval_ms")),
      interval_jitter_(config.getInteger("interval_jitter_ms", 0)) {
  cluster_.addMemberUpdateCb([this](const std::vector<HostSharedPtr>& hosts_added,
                                    const std::vector<HostSharedPtr>& hosts_removed) -> void {
    onClusterMemberUpdate(hosts_added, hosts_removed);
  });
}

void HealthCheckerImplBase::decHealthy() {
  ASSERT(local_process_healthy_ > 0);
  local_process_healthy_--;
  refreshHealthyStat();
}

HealthCheckerStats HealthCheckerImplBase::generateStats(Stats::Scope& scope) {
  std::string prefix("health_check.");
  return {ALL_HEALTH_CHECKER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                   POOL_GAUGE_PREFIX(scope, prefix))};
}

void HealthCheckerImplBase::incHealthy() {
  local_process_healthy_++;
  refreshHealthyStat();
}

std::chrono::milliseconds HealthCheckerImplBase::interval() const {
  // See if the cluster has ever made a connection. If so, we use the defined HC interval. If not,
  // we use a much slower interval to keep the host info relatively up to date in case we suddenly
  // start sending traffic to this cluster. In general host updates are rare and this should
  // greatly smooth out needless health checking.
  uint64_t base_time_ms;
  if (cluster_.info()->stats().upstream_cx_total_.used()) {
    base_time_ms = interval_.count();
  } else {
    base_time_ms = NO_TRAFFIC_INTERVAL.count();
  }

  if (interval_jitter_.count() > 0) {
    base_time_ms += (random_.random() % interval_jitter_.count());
  }

  uint64_t min_interval = runtime_.snapshot().getInteger("health_check.min_interval", 0);
  uint64_t max_interval = runtime_.snapshot().getInteger("health_check.max_interval",
                                                         std::numeric_limits<uint64_t>::max());

  uint64_t final_ms = std::min(base_time_ms, max_interval);
  final_ms = std::max(final_ms, min_interval);
  return std::chrono::milliseconds(final_ms);
}

void HealthCheckerImplBase::addHosts(const std::vector<HostSharedPtr>& hosts) {
  for (const HostSharedPtr& host : hosts) {
    active_sessions_[host] = makeSession(host);
    active_sessions_[host]->start();
  }
}

void HealthCheckerImplBase::onClusterMemberUpdate(const std::vector<HostSharedPtr>& hosts_added,
                                                  const std::vector<HostSharedPtr>& hosts_removed) {
  addHosts(hosts_added);
  for (const HostSharedPtr& host : hosts_removed) {
    auto session_iter = active_sessions_.find(host);
    ASSERT(active_sessions_.end() != session_iter);
    active_sessions_.erase(session_iter);
  }
}

void HealthCheckerImplBase::refreshHealthyStat() {
  // Each hot restarted process health checks independently. To make the stats easier to read,
  // we assume that both processes will converge and the last one that writes wins for the host.
  stats_.healthy_.set(local_process_healthy_);
}

void HealthCheckerImplBase::runCallbacks(HostSharedPtr host, bool changed_state) {
  // When a parent process shuts down, it will kill all of the active health checking sessions,
  // which will decrement the healthy count and the healthy stat in the parent. If the child is
  // stable and does not update, the healthy stat will be wrong. This routine is called any time
  // any HC happens against a host so just refresh the healthy stat here so that it is correct.
  refreshHealthyStat();

  for (const HostStatusCb& cb : callbacks_) {
    cb(host, changed_state);
  }
}

void HealthCheckerImplBase::start() { addHosts(cluster_.hosts()); }

HealthCheckerImplBase::ActiveHealthCheckSession::ActiveHealthCheckSession(
    HealthCheckerImplBase& parent, HostSharedPtr host)
    : host_(host), parent_(parent),
      interval_timer_(parent.dispatcher_.createTimer([this]() -> void { onIntervalBase(); })),
      timeout_timer_(parent.dispatcher_.createTimer([this]() -> void { onTimeoutBase(); })) {

  if (!host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    parent.incHealthy();
  }
}

HealthCheckerImplBase::ActiveHealthCheckSession::~ActiveHealthCheckSession() {
  if (!host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    parent_.decHealthy();
  }
}

void HealthCheckerImplBase::ActiveHealthCheckSession::handleSuccess() {
  // If we are healthy, reset the # of unhealthy to zero.
  num_unhealthy_ = 0;

  bool changed_state = false;
  if (host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    // If this is the first time we ever got a check result on this host, we immediately move
    // it to healthy. This makes startup faster with a small reduction in overall reliability
    // depending on the HC settings.
    if (first_check_ || ++num_healthy_ == parent_.healthy_threshold_) {
      host_->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
      parent_.incHealthy();
      changed_state = true;
    }
  }

  parent_.stats_.success_.inc();
  first_check_ = false;
  parent_.runCallbacks(host_, changed_state);

  timeout_timer_->disableTimer();
  interval_timer_->enableTimer(parent_.interval());
}

void HealthCheckerImplBase::ActiveHealthCheckSession::handleFailure(bool network_failure) {
  // If we are unhealthy, reset the # of healthy to zero.
  num_healthy_ = 0;

  bool changed_state = false;
  if (!host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    if (!network_failure || ++num_unhealthy_ == parent_.unhealthy_threshold_) {
      host_->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
      parent_.decHealthy();
      changed_state = true;
    }
  }

  parent_.stats_.failure_.inc();
  if (network_failure) {
    parent_.stats_.network_failure_.inc();
  }

  first_check_ = false;
  parent_.runCallbacks(host_, changed_state);

  timeout_timer_->disableTimer();
  interval_timer_->enableTimer(parent_.interval());
}

void HealthCheckerImplBase::ActiveHealthCheckSession::onIntervalBase() {
  onInterval();
  timeout_timer_->enableTimer(parent_.timeout_);
  parent_.stats_.attempt_.inc();
}

void HealthCheckerImplBase::ActiveHealthCheckSession::onTimeoutBase() {
  onTimeout();
  handleFailure(true);
}

HttpHealthCheckerImpl::HttpHealthCheckerImpl(const Cluster& cluster, const Json::Object& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random),
      path_(config.getString("path")) {
  if (config.hasObject("service_name")) {
    service_name_.value(config.getString("service_name"));
  }
}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::HttpActiveHealthCheckSession(
    HttpHealthCheckerImpl& parent, HostSharedPtr host)
    : ActiveHealthCheckSession(parent, host), parent_(parent) {}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::~HttpActiveHealthCheckSession() {
  if (client_) {
    // If there is an active request it will get reset, so make sure we ignore the reset.
    expect_reset_ = true;
    client_->close();
  }
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::decodeHeaders(
    Http::HeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!response_headers_);
  response_headers_ = std::move(headers);
  if (end_stream) {
    onResponseComplete();
  }
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onEvent(uint32_t events) {
  if (events & Network::ConnectionEvent::RemoteClose ||
      events & Network::ConnectionEvent::LocalClose) {
    // For the raw disconnect event, we are either between intervals in which case we already have
    // a timer setup, or we did the close or got a reset, in which case we already setup a new
    // timer. There is nothing to do here other than blow away the client.
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onInterval() {
  if (!client_) {
    Upstream::Host::CreateConnectionData conn = host_->createConnection(parent_.dispatcher_);
    client_.reset(parent_.createCodecClient(conn));
    client_->addConnectionCallbacks(*this);
    expect_reset_ = false;
  }

  request_encoder_ = &client_->newStream(*this);
  request_encoder_->getStream().addCallbacks(*this);

  Http::HeaderMapImpl request_headers{
      {Http::Headers::get().Method, "GET"},
      {Http::Headers::get().Host, parent_.cluster_.info()->name()},
      {Http::Headers::get().Path, parent_.path_},
      {Http::Headers::get().UserAgent, Http::Headers::get().UserAgentValues.EnvoyHealthChecker}};

  request_encoder_->encodeHeaders(request_headers, true);
  request_encoder_ = nullptr;
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onResetStream(Http::StreamResetReason) {
  if (expect_reset_) {
    return;
  }

  ENVOY_CONN_LOG(debug, "connection/stream error health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));
  handleFailure(true);
}

bool HttpHealthCheckerImpl::HttpActiveHealthCheckSession::isHealthCheckSucceeded() {
  uint64_t response_code = Http::Utility::getResponseStatus(*response_headers_);
  ENVOY_CONN_LOG(debug, "hc response={} health_flags={}", *client_, response_code,
                 HostUtility::healthFlagsToString(*host_));

  if (response_code != enumToInt(Http::Code::OK)) {
    return false;
  }

  if (parent_.service_name_.valid() &&
      parent_.runtime_.snapshot().featureEnabled("health_check.verify_cluster", 100UL)) {
    parent_.stats_.verify_cluster_.inc();
    std::string service_cluster_healthchecked =
        response_headers_->EnvoyUpstreamHealthCheckedCluster()
            ? response_headers_->EnvoyUpstreamHealthCheckedCluster()->value().c_str()
            : EMPTY_STRING;

    return service_cluster_healthchecked.find(parent_.service_name_.value()) == 0;
  }

  return true;
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onResponseComplete() {
  if (isHealthCheckSucceeded()) {
    handleSuccess();
  } else {
    handleFailure(false);
  }

  if (response_headers_->Connection() &&
      0 ==
          StringUtil::caseInsensitiveCompare(response_headers_->Connection()->value().c_str(),
                                             Http::Headers::get().ConnectionValues.Close.c_str())) {
    client_->close();
  }

  response_headers_.reset();
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onTimeout() {
  ENVOY_CONN_LOG(debug, "connection/stream timeout health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));

  // If there is an active request it will get reset, so make sure we ignore the reset.
  expect_reset_ = true;
  client_->close();
}

Http::CodecClient*
ProdHttpHealthCheckerImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  return new Http::CodecClientProd(Http::CodecClient::Type::HTTP1, std::move(data.connection_),
                                   data.host_description_);
}

TcpHealthCheckMatcher::MatchSegments
TcpHealthCheckMatcher::loadJsonBytes(const std::vector<Json::ObjectSharedPtr>& byte_array) {
  MatchSegments result;

  for (const Json::ObjectSharedPtr& entry : byte_array) {
    std::string hex_string = entry->getString("binary");
    result.push_back(Hex::decode(hex_string));
  }

  return result;
}

bool TcpHealthCheckMatcher::match(const MatchSegments& expected, const Buffer::Instance& buffer) {
  uint64_t start_index = 0;
  for (const std::vector<uint8_t>& segment : expected) {
    ssize_t search_result = buffer.search(&segment[0], segment.size(), start_index);
    if (search_result == -1) {
      return false;
    }

    start_index = search_result + segment.size();
  }

  return true;
}

TcpHealthCheckerImpl::TcpHealthCheckerImpl(const Cluster& cluster, const Json::Object& config,
                                           Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                                           Runtime::RandomGenerator& random)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random),
      send_bytes_(TcpHealthCheckMatcher::loadJsonBytes(config.getObjectArray("send"))),
      receive_bytes_(TcpHealthCheckMatcher::loadJsonBytes(config.getObjectArray("receive"))) {}

TcpHealthCheckerImpl::TcpActiveHealthCheckSession::~TcpActiveHealthCheckSession() {
  if (client_) {
    client_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "total pending buffer={}", *client_, data.length());
  if (TcpHealthCheckMatcher::match(parent_.receive_bytes_, data)) {
    data.drain(data.length());
    handleSuccess();
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onEvent(uint32_t events) {
  if (events & Network::ConnectionEvent::RemoteClose) {
    handleFailure(true);
  }

  if (events & Network::ConnectionEvent::RemoteClose ||
      events & Network::ConnectionEvent::LocalClose) {
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }

  if ((events & Network::ConnectionEvent::Connected) && parent_.receive_bytes_.empty()) {
    // In this case we are just testing that we can connect, so immediately succeed. Also, since
    // we are just doing a connection test, close the connection.
    // NOTE(mattklein123): I've seen cases where the kernel will report a successful connection, and
    // then proceed to fail subsequent calls (so the connection did not actually succeed). I'm not
    // sure what situations cause this. If this turns into a problem, we may need to introduce a
    // timer and see if the connection stays alive for some period of time while waiting to read.
    // (Though we may never get a FIN and won't know until if/when we try to write). In short, this
    // may need to get more complicated but we can start here.
    // TODO(mattklein123): If we had a way on the connection interface to do an immediate read (vs.
    // evented), that would be a good check to run here to make sure it returns the equivalent of
    // EAGAIN. Need to think through how that would look from an interface perspective.
    // TODO(mattklein123): In the case that a user configured bytes to write, they will not be
    // be written, since we currently have no way to know if the bytes actually get written via
    // the connection interface. We might want to figure out how to handle this better later.
    client_->close(Network::ConnectionCloseType::NoFlush);
    handleSuccess();
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onInterval() {
  if (!client_) {
    client_ = host_->createConnection(parent_.dispatcher_).connection_;
    session_callbacks_.reset(new TcpSessionCallbacks(*this));
    client_->addConnectionCallbacks(*session_callbacks_);
    client_->addReadFilter(session_callbacks_);

    client_->connect();
    client_->noDelay(true);
  }

  if (!parent_.send_bytes_.empty()) {
    Buffer::OwnedImpl data;
    for (const std::vector<uint8_t>& segment : parent_.send_bytes_) {
      data.add(&segment[0], segment.size());
    }

    client_->write(data);
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onTimeout() {
  client_->close(Network::ConnectionCloseType::NoFlush);
}

RedisHealthCheckerImpl::RedisHealthCheckerImpl(const Cluster& cluster, const Json::Object& config,
                                               Event::Dispatcher& dispatcher,
                                               Runtime::Loader& runtime,
                                               Runtime::RandomGenerator& random,
                                               Redis::ConnPool::ClientFactory& client_factory)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random),
      client_factory_(client_factory) {}

RedisHealthCheckerImpl::RedisActiveHealthCheckSession::RedisActiveHealthCheckSession(
    RedisHealthCheckerImpl& parent, HostSharedPtr host)
    : ActiveHealthCheckSession(parent, host), parent_(parent) {}

RedisHealthCheckerImpl::RedisActiveHealthCheckSession::~RedisActiveHealthCheckSession() {
  if (current_request_) {
    current_request_->cancel();
    current_request_ = nullptr;
  }

  if (client_) {
    client_->close();
  }
}

void RedisHealthCheckerImpl::RedisActiveHealthCheckSession::onEvent(uint32_t events) {
  if (events & Network::ConnectionEvent::RemoteClose ||
      events & Network::ConnectionEvent::LocalClose) {
    // This should only happen after any active requests have been failed/cancelled.
    ASSERT(!current_request_);
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

void RedisHealthCheckerImpl::RedisActiveHealthCheckSession::onInterval() {
  if (!client_) {
    client_ = parent_.client_factory_.create(host_, parent_.dispatcher_, *this);
    client_->addConnectionCallbacks(*this);
  }

  ASSERT(!current_request_);
  current_request_ = client_->makeRequest(healthCheckRequest(), *this);
}

void RedisHealthCheckerImpl::RedisActiveHealthCheckSession::onResponse(
    Redis::RespValuePtr&& value) {
  current_request_ = nullptr;
  if (value->type() == Redis::RespType::SimpleString && value->asString() == "PONG") {
    handleSuccess();
  } else {
    handleFailure(false);
  }
}

void RedisHealthCheckerImpl::RedisActiveHealthCheckSession::onFailure() {
  current_request_ = nullptr;
  handleFailure(true);
}

void RedisHealthCheckerImpl::RedisActiveHealthCheckSession::onTimeout() {
  current_request_->cancel();
  current_request_ = nullptr;
  client_->close();
}

RedisHealthCheckerImpl::HealthCheckRequest::HealthCheckRequest() {
  std::vector<Redis::RespValue> values(1);
  values[0].type(Redis::RespType::BulkString);
  values[0].asString() = "PING";
  request_.type(Redis::RespType::Array);
  request_.asArray().swap(values);
}

} // namespace Upstream
} // namespace Envoy
