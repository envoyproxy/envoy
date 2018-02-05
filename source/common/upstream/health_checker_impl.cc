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
#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/hex.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codec_client.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"
#include "common/redis/conn_pool_impl.h"
#include "common/upstream/host_utility.h"

namespace Envoy {
namespace Upstream {

HealthCheckerSharedPtr
HealthCheckerFactory::create(const envoy::api::v2::core::HealthCheck& hc_config,
                             Upstream::Cluster& cluster, Runtime::Loader& runtime,
                             Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher) {
  switch (hc_config.health_checker_case()) {
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kHttpHealthCheck:
    return std::make_shared<ProdHttpHealthCheckerImpl>(cluster, hc_config, dispatcher, runtime,
                                                       random);
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kTcpHealthCheck:
    return std::make_shared<TcpHealthCheckerImpl>(cluster, hc_config, dispatcher, runtime, random);
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kRedisHealthCheck:
    return std::make_shared<RedisHealthCheckerImpl>(cluster, hc_config, dispatcher, runtime, random,
                                                    Redis::ConnPool::ClientFactoryImpl::instance_);
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kGrpcHealthCheck:
    if (!(cluster.info()->features() & Upstream::ClusterInfo::Features::HTTP2)) {
      throw EnvoyException(fmt::format("{} cluster must support HTTP/2 for gRPC healthchecking",
                                       cluster.info()->name()));
    }
    return std::make_shared<ProdGrpcHealthCheckerImpl>(cluster, hc_config, dispatcher, runtime,
                                                       random);
  default:
    // TODO(htuch): This should be subsumed eventually by the constraint checking in #1308.
    throw EnvoyException("Health checker type not set");
  }
}

const std::chrono::milliseconds HealthCheckerImplBase::NO_TRAFFIC_INTERVAL{60000};

HealthCheckerImplBase::HealthCheckerImplBase(const Cluster& cluster,
                                             const envoy::api::v2::core::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random)
    : cluster_(cluster), dispatcher_(dispatcher),
      timeout_(PROTOBUF_GET_MS_REQUIRED(config, timeout)),
      unhealthy_threshold_(PROTOBUF_GET_WRAPPED_REQUIRED(config, unhealthy_threshold)),
      healthy_threshold_(PROTOBUF_GET_WRAPPED_REQUIRED(config, healthy_threshold)),
      stats_(generateStats(cluster.info()->statsScope())), runtime_(runtime), random_(random),
      reuse_connection_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, reuse_connection, true)),
      interval_(PROTOBUF_GET_MS_REQUIRED(config, interval)),
      interval_jitter_(PROTOBUF_GET_MS_OR_DEFAULT(config, interval_jitter, 0)) {
  cluster_.prioritySet().addMemberUpdateCb(
      [this](uint32_t, const HostVector& hosts_added, const HostVector& hosts_removed) -> void {
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

void HealthCheckerImplBase::addHosts(const HostVector& hosts) {
  for (const HostSharedPtr& host : hosts) {
    active_sessions_[host] = makeSession(host);
    host->setHealthChecker(
        HealthCheckHostMonitorPtr{new HealthCheckHostMonitorImpl(shared_from_this(), host)});
    active_sessions_[host]->start();
  }
}

void HealthCheckerImplBase::onClusterMemberUpdate(const HostVector& hosts_added,
                                                  const HostVector& hosts_removed) {
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

void HealthCheckerImplBase::HealthCheckHostMonitorImpl::setUnhealthy() {
  // This is called cross thread. The cluster/health checker might already be gone.
  std::shared_ptr<HealthCheckerImplBase> health_checker = health_checker_.lock();
  if (health_checker) {
    health_checker->setUnhealthyCrossThread(host_.lock());
  }
}

void HealthCheckerImplBase::setUnhealthyCrossThread(const HostSharedPtr& host) {
  // The threading here is complex. The cluster owns the only strong reference to the health
  // checker. It might go away when we post to the main thread from a worker thread. To deal with
  // this we use the following sequence of events:
  // 1) We capture a weak reference to the health checker and post it from worker thread to main
  //    thread.
  // 2) On the main thread, we make sure it is still valid (as the cluster may have been destroyed).
  // 3) Additionally, the host/session may also be gone by then so we check that also.
  std::weak_ptr<HealthCheckerImplBase> weak_this = shared_from_this();
  dispatcher_.post([weak_this, host]() -> void {
    std::shared_ptr<HealthCheckerImplBase> shared_this = weak_this.lock();
    if (shared_this == nullptr) {
      return;
    }

    const auto session = shared_this->active_sessions_.find(host);
    if (session == shared_this->active_sessions_.end()) {
      return;
    }

    session->second->setUnhealthy(ActiveHealthCheckSession::FailureType::Passive);
  });
}

void HealthCheckerImplBase::start() {
  for (auto& host_set : cluster_.prioritySet().hostSetsPerPriority()) {
    addHosts(host_set->hosts());
  }
}

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

void HealthCheckerImplBase::ActiveHealthCheckSession::setUnhealthy(FailureType type) {
  // If we are unhealthy, reset the # of healthy to zero.
  num_healthy_ = 0;

  bool changed_state = false;
  if (!host_->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    if (type != FailureType::Network || ++num_unhealthy_ == parent_.unhealthy_threshold_) {
      host_->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
      parent_.decHealthy();
      changed_state = true;
    }
  }

  parent_.stats_.failure_.inc();
  if (type == FailureType::Network) {
    parent_.stats_.network_failure_.inc();
  } else if (type == FailureType::Passive) {
    parent_.stats_.passive_failure_.inc();
  }

  first_check_ = false;
  parent_.runCallbacks(host_, changed_state);
}

void HealthCheckerImplBase::ActiveHealthCheckSession::handleFailure(FailureType type) {
  setUnhealthy(type);
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
  handleFailure(FailureType::Network);
}

HttpHealthCheckerImpl::HttpHealthCheckerImpl(const Cluster& cluster,
                                             const envoy::api::v2::core::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random),
      path_(config.http_health_check().path()) {
  if (!config.http_health_check().service_name().empty()) {
    service_name_.value(config.http_health_check().service_name());
  }
}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::HttpActiveHealthCheckSession(
    HttpHealthCheckerImpl& parent, const HostSharedPtr& host)
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

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // For the raw disconnect event, we are either between intervals in which case we already have
    // a timer setup, or we did the close or got a reset, in which case we already setup a new
    // timer. There is nothing to do here other than blow away the client.
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onInterval() {
  if (!client_) {
    Upstream::Host::CreateConnectionData conn =
        host_->createConnection(parent_.dispatcher_, nullptr);
    client_.reset(parent_.createCodecClient(conn));
    client_->addConnectionCallbacks(connection_callback_impl_);
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
  handleFailure(FailureType::Network);
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
    handleFailure(FailureType::Active);
  }

  if ((response_headers_->Connection() &&
       0 == StringUtil::caseInsensitiveCompare(
                response_headers_->Connection()->value().c_str(),
                Http::Headers::get().ConnectionValues.Close.c_str())) ||
      !parent_.reuse_connection_) {
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

TcpHealthCheckMatcher::MatchSegments TcpHealthCheckMatcher::loadProtoBytes(
    const Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload>& byte_array) {
  MatchSegments result;

  for (const auto& entry : byte_array) {
    const std::string& hex_string = entry.text();
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

TcpHealthCheckerImpl::TcpHealthCheckerImpl(const Cluster& cluster,
                                           const envoy::api::v2::core::HealthCheck& config,
                                           Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                                           Runtime::RandomGenerator& random)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random), send_bytes_([&config] {
        Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload> send_repeated;
        if (!config.tcp_health_check().send().text().empty()) {
          send_repeated.Add()->CopyFrom(config.tcp_health_check().send());
        }
        return TcpHealthCheckMatcher::loadProtoBytes(send_repeated);
      }()),
      receive_bytes_(TcpHealthCheckMatcher::loadProtoBytes(config.tcp_health_check().receive())) {}

TcpHealthCheckerImpl::TcpActiveHealthCheckSession::~TcpActiveHealthCheckSession() {
  if (client_) {
    client_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "total pending buffer={}", *client_, data.length());
  if (TcpHealthCheckMatcher::match(parent_.receive_bytes_, data)) {
    ENVOY_CONN_LOG(trace, "healthcheck passed", *client_);
    data.drain(data.length());
    handleSuccess();
    if (!parent_.reuse_connection_) {
      client_->close(Network::ConnectionCloseType::NoFlush);
    }
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose) {
    handleFailure(FailureType::Network);
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }

  if (event == Network::ConnectionEvent::Connected && parent_.receive_bytes_.empty()) {
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
    client_ = host_->createConnection(parent_.dispatcher_, nullptr).connection_;
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

RedisHealthCheckerImpl::RedisHealthCheckerImpl(const Cluster& cluster,
                                               const envoy::api::v2::core::HealthCheck& config,
                                               Event::Dispatcher& dispatcher,
                                               Runtime::Loader& runtime,
                                               Runtime::RandomGenerator& random,
                                               Redis::ConnPool::ClientFactory& client_factory)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random),
      client_factory_(client_factory) {}

RedisHealthCheckerImpl::RedisActiveHealthCheckSession::RedisActiveHealthCheckSession(
    RedisHealthCheckerImpl& parent, const HostSharedPtr& host)
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

void RedisHealthCheckerImpl::RedisActiveHealthCheckSession::onEvent(
    Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
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
    handleFailure(FailureType::Active);
  }
  if (!parent_.reuse_connection_) {
    client_->close();
  }
}

void RedisHealthCheckerImpl::RedisActiveHealthCheckSession::onFailure() {
  current_request_ = nullptr;
  handleFailure(FailureType::Network);
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

GrpcHealthCheckerImpl::GrpcHealthCheckerImpl(const Cluster& cluster,
                                             const envoy::api::v2::core::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "grpc.health.v1.Health.Check")) {
  if (!config.grpc_health_check().service_name().empty()) {
    service_name_.value(config.grpc_health_check().service_name());
  }
}

GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::GrpcActiveHealthCheckSession(
    GrpcHealthCheckerImpl& parent, const HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent) {}

GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::~GrpcActiveHealthCheckSession() {
  if (client_) {
    // If there is an active request it will get reset, so make sure we ignore the reset.
    expect_reset_ = true;
    client_->close();
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::decodeHeaders(
    Http::HeaderMapPtr&& headers, bool end_stream) {
  const auto http_response_status = Http::Utility::getResponseStatus(*headers);
  if (http_response_status != enumToInt(Http::Code::OK)) {
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md requires that
    // grpc-status be used if available.
    if (end_stream) {
      const auto grpc_status = Grpc::Common::getGrpcStatus(*headers);
      if (grpc_status.valid()) {
        onRpcComplete(grpc_status.value(), Grpc::Common::getGrpcMessage(*headers), true);
        return;
      }
    }
    onRpcComplete(Grpc::Common::httpToGrpcStatus(http_response_status), "non-200 HTTP response",
                  end_stream);
    return;
  }
  if (!Grpc::Common::hasGrpcContentType(*headers)) {
    onRpcComplete(Grpc::Status::GrpcStatus::Internal, "invalid gRPC content-type", false);
    return;
  }
  if (end_stream) {
    // This is how, for instance, grpc-go signals about missing service - HTTP/2 200 OK with
    // 'unimplemented' gRPC status.
    const auto grpc_status = Grpc::Common::getGrpcStatus(*headers);
    if (grpc_status.valid()) {
      onRpcComplete(grpc_status.value(), Grpc::Common::getGrpcMessage(*headers), true);
      return;
    }
    onRpcComplete(Grpc::Status::GrpcStatus::Internal,
                  "gRPC protocol violation: unexpected stream end", true);
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::decodeData(Buffer::Instance& data,
                                                                     bool end_stream) {
  if (end_stream) {
    onRpcComplete(Grpc::Status::GrpcStatus::Internal,
                  "gRPC protocol violation: unexpected stream end", true);
    return;
  }

  // We should end up with only one frame here.
  std::vector<Grpc::Frame> decoded_frames;
  if (!decoder_.decode(data, decoded_frames)) {
    onRpcComplete(Grpc::Status::GrpcStatus::Internal, "gRPC wire protocol decode error", false);
  }
  for (auto& frame : decoded_frames) {
    if (frame.length_ > 0) {
      if (health_check_response_) {
        // grpc.health.v1.Health.Check is unary RPC, so only one message is allowed.
        onRpcComplete(Grpc::Status::GrpcStatus::Internal, "unexpected streaming", false);
        return;
      }
      health_check_response_ = std::make_unique<grpc::health::v1::HealthCheckResponse>();
      Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));

      if (frame.flags_ != Grpc::GRPC_FH_DEFAULT ||
          !health_check_response_->ParseFromZeroCopyStream(&stream)) {
        onRpcComplete(Grpc::Status::GrpcStatus::Internal, "invalid grpc.health.v1 RPC payload",
                      false);
        return;
      }
    }
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::decodeTrailers(
    Http::HeaderMapPtr&& trailers) {
  auto maybe_grpc_status = Grpc::Common::getGrpcStatus(*trailers);
  auto grpc_status =
      maybe_grpc_status.valid() ? maybe_grpc_status.value() : Grpc::Status::GrpcStatus::Internal;
  const std::string grpc_message =
      maybe_grpc_status.valid() ? Grpc::Common::getGrpcMessage(*trailers) : "invalid gRPC status";
  onRpcComplete(grpc_status, grpc_message, true);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // For the raw disconnect event, we are either between intervals in which case we already have
    // a timer setup, or we did the close or got a reset, in which case we already setup a new
    // timer. There is nothing to do here other than blow away the client.
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onInterval() {
  if (!client_) {
    Upstream::Host::CreateConnectionData conn =
        host_->createConnection(parent_.dispatcher_, nullptr);
    client_ = parent_.createCodecClient(conn);
    client_->addConnectionCallbacks(connection_callback_impl_);
    client_->setCodecConnectionCallbacks(http_connection_callback_impl_);
  }

  request_encoder_ = &client_->newStream(*this);
  request_encoder_->getStream().addCallbacks(*this);

  auto headers_message = Grpc::Common::prepareHeaders(
      parent_.cluster_.info()->name(), parent_.service_method_.service()->full_name(),
      parent_.service_method_.name());
  headers_message->headers().insertUserAgent().value().setReference(
      Http::Headers::get().UserAgentValues.EnvoyHealthChecker);

  request_encoder_->encodeHeaders(headers_message->headers(), false);

  grpc::health::v1::HealthCheckRequest request;
  if (parent_.service_name_.valid()) {
    request.set_service(parent_.service_name_.value());
  }

  request_encoder_->encodeData(*Grpc::Common::serializeBody(request), true);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onResetStream(Http::StreamResetReason) {
  const bool expected_reset = expect_reset_;
  resetState();

  if (expected_reset) {
    // Stream reset was initiated by us (bogus gRPC response, timeout or cluster host is going
    // away). In these cases health check failure has already been reported, so just return.
    return;
  }

  ENVOY_CONN_LOG(debug, "connection/stream error health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));

  // TODO(baranov1ch): according to all HTTP standards, we should check if reason is one of
  // Http::StreamResetReason::RemoteRefusedStreamReset (which may mean GOAWAY),
  // Http::StreamResetReason::RemoteReset or Http::StreamResetReason::ConnectionTermination (both
  // mean connection close), check if connection is not fresh (was used for at least 1 request)
  // and silently retry request on the fresh connection. This is also true for HTTP/1.1 healthcheck.
  handleFailure(FailureType::Network);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onGoAway() {
  ENVOY_CONN_LOG(debug, "connection going away health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));
  // Even if we have active health check probe, fail it on GOAWAY and schedule new one.
  if (request_encoder_) {
    handleFailure(FailureType::Network);
    expect_reset_ = true;
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }
  client_->close();
}

bool GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::isHealthCheckSucceeded(
    Grpc::Status::GrpcStatus grpc_status) const {
  if (grpc_status != Grpc::Status::GrpcStatus::Ok) {
    return false;
  }

  if (!health_check_response_ ||
      health_check_response_->status() != grpc::health::v1::HealthCheckResponse::SERVING) {
    return false;
  }

  return true;
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onRpcComplete(
    Grpc::Status::GrpcStatus grpc_status, const std::string& grpc_message, bool end_stream) {
  logHealthCheckStatus(grpc_status, grpc_message);
  if (isHealthCheckSucceeded(grpc_status)) {
    handleSuccess();
  } else {
    handleFailure(FailureType::Active);
  }

  // |end_stream| will be false if we decided to stop healthcheck before HTTP stream has ended -
  // invalid gRPC payload, unexpected message stream or wrong content-type.
  if (end_stream) {
    resetState();
  } else {
    // resetState() will be called by onResetStream().
    expect_reset_ = true;
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }

  if (!parent_.reuse_connection_) {
    client_->close();
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::resetState() {
  expect_reset_ = false;
  request_encoder_ = nullptr;
  decoder_ = Grpc::Decoder();
  health_check_response_.reset();
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onTimeout() {
  ENVOY_CONN_LOG(debug, "connection/stream timeout health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));
  expect_reset_ = true;
  request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::logHealthCheckStatus(
    Grpc::Status::GrpcStatus grpc_status, const std::string& grpc_message) {
  const char* service_status;
  if (!health_check_response_) {
    service_status = "rpc_error";
  } else {
    switch (health_check_response_->status()) {
    case grpc::health::v1::HealthCheckResponse::SERVING:
      service_status = "serving";
      break;
    case grpc::health::v1::HealthCheckResponse::NOT_SERVING:
      service_status = "not_serving";
      break;
    case grpc::health::v1::HealthCheckResponse::UNKNOWN:
      service_status = "unknown";
      break;
    default:
      // Should not happen really, Protobuf should not parse undefined enums values.
      NOT_REACHED;
      break;
    }
  }
  std::string grpc_status_message;
  if (grpc_status != Grpc::Status::GrpcStatus::Ok && !grpc_message.empty()) {
    grpc_status_message = fmt::format("{} ({})", grpc_status, grpc_message);
  } else {
    grpc_status_message = fmt::format("{}", grpc_status);
  }

  // The following can be compiled out when defining NVLOG.
  // TODO(mattklein123): Refactor this to make this less ugly.
  UNREFERENCED_PARAMETER(service_status);
  UNREFERENCED_PARAMETER(grpc_status_message);
  ENVOY_CONN_LOG(debug, "hc grpc_status={} service_status={} health_flags={}", *client_,
                 grpc_status_message, service_status, HostUtility::healthFlagsToString(*host_));
}

Http::CodecClientPtr
ProdGrpcHealthCheckerImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  return std::make_unique<Http::CodecClientProd>(
      Http::CodecClient::Type::HTTP2, std::move(data.connection_), data.host_description_);
}

} // namespace Upstream
} // namespace Envoy
