#include "common/upstream/health_checker_impl.h"

#include "envoy/server/health_checker_config.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/grpc/common.h"
#include "common/http/header_map_impl.h"
#include "common/router/router.h"
#include "common/upstream/host_utility.h"

// TODO(dio): Remove dependency to extension health checkers when redis_health_check is removed.
#include "extensions/health_checkers/well_known_names.h"

namespace Envoy {
namespace Upstream {

class HealthCheckerFactoryContextImpl : public Server::Configuration::HealthCheckerFactoryContext {
public:
  HealthCheckerFactoryContextImpl(Upstream::Cluster& cluster, Envoy::Runtime::Loader& runtime,
                                  Envoy::Runtime::RandomGenerator& random,
                                  Event::Dispatcher& dispatcher)
      : cluster_(cluster), runtime_(runtime), random_(random), dispatcher_(dispatcher) {}
  Upstream::Cluster& cluster() override { return cluster_; }
  Envoy::Runtime::Loader& runtime() override { return runtime_; }
  Envoy::Runtime::RandomGenerator& random() override { return random_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }

private:
  Upstream::Cluster& cluster_;
  Envoy::Runtime::Loader& runtime_;
  Envoy::Runtime::RandomGenerator& random_;
  Event::Dispatcher& dispatcher_;
};

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
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kGrpcHealthCheck:
    if (!(cluster.info()->features() & Upstream::ClusterInfo::Features::HTTP2)) {
      throw EnvoyException(fmt::format("{} cluster must support HTTP/2 for gRPC healthchecking",
                                       cluster.info()->name()));
    }
    return std::make_shared<ProdGrpcHealthCheckerImpl>(cluster, hc_config, dispatcher, runtime,
                                                       random);
  // Deprecated redis_health_check, preserving using old config until it is removed.
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kRedisHealthCheck:
    ENVOY_LOG(warn, "redis_health_check is deprecated, use custom_health_check instead");
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kCustomHealthCheck: {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::CustomHealthCheckerFactory>(
            hc_config.has_redis_health_check()
                ? Extensions::HealthCheckers::HealthCheckerNames::get().REDIS_HEALTH_CHECKER
                : hc_config.custom_health_check().name());
    std::unique_ptr<Server::Configuration::HealthCheckerFactoryContext> context(
        new HealthCheckerFactoryContextImpl(cluster, runtime, random, dispatcher));
    return factory.createCustomHealthChecker(hc_config, *context);
  }
  default:
    // Checked by schema.
    NOT_REACHED;
  }
}

HttpHealthCheckerImpl::HttpHealthCheckerImpl(const Cluster& cluster,
                                             const envoy::api::v2::core::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random),
      path_(config.http_health_check().path()), host_value_(config.http_health_check().host()),
      request_headers_parser_(
          Router::HeaderParser::configure(config.http_health_check().request_headers_to_add())) {
  if (!config.http_health_check().service_name().empty()) {
    service_name_ = config.http_health_check().service_name();
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

const RequestInfo::RequestInfoImpl
    HttpHealthCheckerImpl::HttpActiveHealthCheckSession::REQUEST_INFO;

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onInterval() {
  if (!client_) {
    Upstream::Host::CreateConnectionData conn =
        host_->createHealthCheckConnection(parent_.dispatcher_);
    client_.reset(parent_.createCodecClient(conn));
    client_->addConnectionCallbacks(connection_callback_impl_);
    expect_reset_ = false;
  }

  request_encoder_ = &client_->newStream(*this);
  request_encoder_->getStream().addCallbacks(*this);

  Http::HeaderMapImpl request_headers{
      {Http::Headers::get().Method, "GET"},
      {Http::Headers::get().Host,
       parent_.host_value_.empty() ? parent_.cluster_.info()->name() : parent_.host_value_},
      {Http::Headers::get().Path, parent_.path_},
      {Http::Headers::get().UserAgent, Http::Headers::get().UserAgentValues.EnvoyHealthChecker}};

  parent_.request_headers_parser_->evaluateHeaders(request_headers, REQUEST_INFO);
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

  if (parent_.service_name_ &&
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
                                   data.host_description_, dispatcher_);
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
    client_ = host_->createHealthCheckConnection(parent_.dispatcher_).connection_;
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

    client_->write(data, false);
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onTimeout() {
  client_->close(Network::ConnectionCloseType::NoFlush);
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
    service_name_ = config.grpc_health_check().service_name();
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
      if (grpc_status) {
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
    if (grpc_status) {
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
      maybe_grpc_status ? maybe_grpc_status.value() : Grpc::Status::GrpcStatus::Internal;
  const std::string grpc_message =
      maybe_grpc_status ? Grpc::Common::getGrpcMessage(*trailers) : "invalid gRPC status";
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
        host_->createHealthCheckConnection(parent_.dispatcher_);
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
  Router::FilterUtility::setUpstreamScheme(headers_message->headers(), *parent_.cluster_.info());

  request_encoder_->encodeHeaders(headers_message->headers(), false);

  grpc::health::v1::HealthCheckRequest request;
  if (parent_.service_name_) {
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

  ENVOY_CONN_LOG(debug, "hc grpc_status={} service_status={} health_flags={}", *client_,
                 grpc_status_message, service_status, HostUtility::healthFlagsToString(*host_));
}

Http::CodecClientPtr
ProdGrpcHealthCheckerImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  return std::make_unique<Http::CodecClientProd>(Http::CodecClient::Type::HTTP2,
                                                 std::move(data.connection_),
                                                 data.host_description_, dispatcher_);
}

} // namespace Upstream
} // namespace Envoy