#include "common/upstream/health_checker_impl.h"

#include "envoy/server/health_checker_config.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/macros.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/grpc/common.h"
#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/router/router.h"
#include "common/runtime/runtime_impl.h"
#include "common/upstream/host_utility.h"

// TODO(dio): Remove dependency to extension health checkers when redis_health_check is removed.
#include "extensions/health_checkers/well_known_names.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Upstream {

class HealthCheckerFactoryContextImpl : public Server::Configuration::HealthCheckerFactoryContext {
public:
  HealthCheckerFactoryContextImpl(Upstream::Cluster& cluster, Envoy::Runtime::Loader& runtime,
                                  Envoy::Runtime::RandomGenerator& random,
                                  Event::Dispatcher& dispatcher,
                                  HealthCheckEventLoggerPtr&& event_logger,
                                  ProtobufMessage::ValidationVisitor& validation_visitor,
                                  Api::Api& api)
      : cluster_(cluster), runtime_(runtime), random_(random), dispatcher_(dispatcher),
        event_logger_(std::move(event_logger)), validation_visitor_(validation_visitor), api_(api) {
  }
  Upstream::Cluster& cluster() override { return cluster_; }
  Envoy::Runtime::Loader& runtime() override { return runtime_; }
  Envoy::Runtime::RandomGenerator& random() override { return random_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  HealthCheckEventLoggerPtr eventLogger() override { return std::move(event_logger_); }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Api::Api& api() override { return api_; }

private:
  Upstream::Cluster& cluster_;
  Envoy::Runtime::Loader& runtime_;
  Envoy::Runtime::RandomGenerator& random_;
  Event::Dispatcher& dispatcher_;
  HealthCheckEventLoggerPtr event_logger_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Api::Api& api_;
};

HealthCheckerSharedPtr HealthCheckerFactory::create(
    const envoy::api::v2::core::HealthCheck& health_check_config, Upstream::Cluster& cluster,
    Runtime::Loader& runtime, Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
    AccessLog::AccessLogManager& log_manager,
    ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api) {
  HealthCheckEventLoggerPtr event_logger;
  if (!health_check_config.event_log_path().empty()) {
    event_logger = std::make_unique<HealthCheckEventLoggerImpl>(
        log_manager, dispatcher.timeSource(), health_check_config.event_log_path());
  }
  switch (health_check_config.health_checker_case()) {
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kHttpHealthCheck:
    return std::make_shared<ProdHttpHealthCheckerImpl>(cluster, health_check_config, dispatcher,
                                                       runtime, random, std::move(event_logger));
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kTcpHealthCheck:
    return std::make_shared<TcpHealthCheckerImpl>(cluster, health_check_config, dispatcher, runtime,
                                                  random, std::move(event_logger));
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kGrpcHealthCheck:
    if (!(cluster.info()->features() & Upstream::ClusterInfo::Features::HTTP2)) {
      throw EnvoyException(fmt::format("{} cluster must support HTTP/2 for gRPC healthchecking",
                                       cluster.info()->name()));
    }
    return std::make_shared<ProdGrpcHealthCheckerImpl>(cluster, health_check_config, dispatcher,
                                                       runtime, random, std::move(event_logger));
  case envoy::api::v2::core::HealthCheck::HealthCheckerCase::kCustomHealthCheck: {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::CustomHealthCheckerFactory>(
            health_check_config.custom_health_check().name());
    std::unique_ptr<Server::Configuration::HealthCheckerFactoryContext> context(
        new HealthCheckerFactoryContextImpl(cluster, runtime, random, dispatcher,
                                            std::move(event_logger), validation_visitor, api));
    return factory.createCustomHealthChecker(health_check_config, *context);
  }
  default:
    // Checked by schema.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

HttpHealthCheckerImpl::HttpHealthCheckerImpl(const Cluster& cluster,
                                             const envoy::api::v2::core::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random,
                                             HealthCheckEventLoggerPtr&& event_logger)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random, std::move(event_logger)),
      path_(config.http_health_check().path()), host_value_(config.http_health_check().host()),
      request_headers_parser_(
          Router::HeaderParser::configure(config.http_health_check().request_headers_to_add(),
                                          config.http_health_check().request_headers_to_remove())),
      http_status_checker_(config.http_health_check().expected_statuses(),
                           static_cast<uint64_t>(Http::Code::OK)),
      codec_client_type_(codecClientType(config.http_health_check().use_http2()
                                             ? envoy::type::HTTP2
                                             : config.http_health_check().codec_client_type())) {
  if (!config.http_health_check().service_name().empty()) {
    service_name_ = config.http_health_check().service_name();
  }
}

HttpHealthCheckerImpl::HttpStatusChecker::HttpStatusChecker(
    const Protobuf::RepeatedPtrField<envoy::type::Int64Range>& expected_statuses,
    uint64_t default_expected_status) {
  for (const auto& status_range : expected_statuses) {
    const auto start = status_range.start();
    const auto end = status_range.end();

    if (start >= end) {
      throw EnvoyException(fmt::format(
          "Invalid http status range: expecting start < end, but found start={} and end={}", start,
          end));
    }

    if (start < 100) {
      throw EnvoyException(fmt::format(
          "Invalid http status range: expecting start >= 100, but found start={}", start));
    }

    if (end > 600) {
      throw EnvoyException(
          fmt::format("Invalid http status range: expecting end <= 600, but found end={}", end));
    }

    ranges_.emplace_back(std::make_pair(static_cast<uint64_t>(start), static_cast<uint64_t>(end)));
  }

  if (ranges_.empty()) {
    ranges_.emplace_back(std::make_pair(default_expected_status, default_expected_status + 1));
  }
}

bool HttpHealthCheckerImpl::HttpStatusChecker::inRange(uint64_t http_status) const {
  for (const auto& range : ranges_) {
    if (http_status >= range.first && http_status < range.second) {
      return true;
    }
  }

  return false;
}

Http::Protocol codecClientTypeToProtocol(Http::CodecClient::Type codec_client_type) {
  switch (codec_client_type) {
  case Http::CodecClient::Type::HTTP1:
    return Http::Protocol::Http11;
  case Http::CodecClient::Type::HTTP2:
    return Http::Protocol::Http2;
  case Http::CodecClient::Type::HTTP3:
    return Http::Protocol::Http3;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::HttpActiveHealthCheckSession(
    HttpHealthCheckerImpl& parent, const HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent),
      hostname_(parent_.host_value_.empty() ? parent_.cluster_.info()->name()
                                            : parent_.host_value_),
      protocol_(codecClientTypeToProtocol(parent_.codec_client_type_)),
      local_address_(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1")) {}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::~HttpActiveHealthCheckSession() {
  ASSERT(client_ == nullptr);
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onDeferredDelete() {
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
    response_headers_.reset();
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

// TODO(lilika) : Support connection pooling
void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onInterval() {
  if (!client_) {
    Upstream::Host::CreateConnectionData conn =
        host_->createHealthCheckConnection(parent_.dispatcher_);
    client_.reset(parent_.createCodecClient(conn));
    client_->addConnectionCallbacks(connection_callback_impl_);
    expect_reset_ = false;
  }

  Http::StreamEncoder* request_encoder = &client_->newStream(*this);
  request_encoder->getStream().addCallbacks(*this);

  Http::HeaderMapImpl request_headers{
      {Http::Headers::get().Method, "GET"},
      {Http::Headers::get().Host, hostname_},
      {Http::Headers::get().Path, parent_.path_},
      {Http::Headers::get().UserAgent, Http::Headers::get().UserAgentValues.EnvoyHealthChecker}};
  Router::FilterUtility::setUpstreamScheme(
      request_headers, host_->transportSocketFactory().implementsSecureTransport());
  StreamInfo::StreamInfoImpl stream_info(protocol_, parent_.dispatcher_.timeSource());
  stream_info.setDownstreamLocalAddress(local_address_);
  stream_info.setDownstreamRemoteAddress(local_address_);
  stream_info.onUpstreamHostSelected(host_);
  parent_.request_headers_parser_->evaluateHeaders(request_headers, stream_info);
  request_encoder->encodeHeaders(request_headers, true);
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onResetStream(Http::StreamResetReason,
                                                                        absl::string_view) {
  if (expect_reset_) {
    return;
  }

  ENVOY_CONN_LOG(debug, "connection/stream error health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));
  handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType::NETWORK);
}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::HealthCheckResult
HttpHealthCheckerImpl::HttpActiveHealthCheckSession::healthCheckResult() {
  uint64_t response_code = Http::Utility::getResponseStatus(*response_headers_);
  ENVOY_CONN_LOG(debug, "hc response={} health_flags={}", *client_, response_code,
                 HostUtility::healthFlagsToString(*host_));

  if (!parent_.http_status_checker_.inRange(response_code)) {
    return HealthCheckResult::Failed;
  }

  const auto degraded = response_headers_->EnvoyDegraded() != nullptr;

  if (parent_.service_name_ &&
      parent_.runtime_.snapshot().featureEnabled("health_check.verify_cluster", 100UL)) {
    parent_.stats_.verify_cluster_.inc();
    std::string service_cluster_healthchecked =
        response_headers_->EnvoyUpstreamHealthCheckedCluster()
            ? std::string(
                  response_headers_->EnvoyUpstreamHealthCheckedCluster()->value().getStringView())
            : EMPTY_STRING;

    if (absl::StartsWith(service_cluster_healthchecked, parent_.service_name_.value())) {
      return degraded ? HealthCheckResult::Degraded : HealthCheckResult::Succeeded;
    } else {
      return HealthCheckResult::Failed;
    }
  }

  return degraded ? HealthCheckResult::Degraded : HealthCheckResult::Succeeded;
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onResponseComplete() {
  switch (healthCheckResult()) {
  case HealthCheckResult::Succeeded:
    handleSuccess(false);
    break;
  case HealthCheckResult::Degraded:
    handleSuccess(true);
    break;
  case HealthCheckResult::Failed:
    host_->setActiveHealthFailureType(Host::ActiveHealthFailureType::UNHEALTHY);
    handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType::ACTIVE);
    break;
  }

  if (shouldClose()) {
    client_->close();
  }

  response_headers_.reset();
}

// It is possible for this session to have been deferred destroyed inline in handleFailure()
// above so make sure we still have a connection that we might need to close.
bool HttpHealthCheckerImpl::HttpActiveHealthCheckSession::shouldClose() const {
  if (client_ == nullptr) {
    return false;
  }

  if (response_headers_->Connection()) {
    const bool close =
        absl::EqualsIgnoreCase(response_headers_->Connection()->value().getStringView(),
                               Http::Headers::get().ConnectionValues.Close);
    if (close) {
      return true;
    }
  }

  if (response_headers_->ProxyConnection() && protocol_ < Http::Protocol::Http2) {
    const bool close =
        absl::EqualsIgnoreCase(response_headers_->ProxyConnection()->value().getStringView(),
                               Http::Headers::get().ConnectionValues.Close);
    if (close) {
      return true;
    }
  }

  if (!parent_.reuse_connection_) {
    return true;
  }

  return false;
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onTimeout() {
  if (client_) {
    host_->setActiveHealthFailureType(Host::ActiveHealthFailureType::TIMEOUT);
    ENVOY_CONN_LOG(debug, "connection/stream timeout health_flags={}", *client_,
                   HostUtility::healthFlagsToString(*host_));

    // If there is an active request it will get reset, so make sure we ignore the reset.
    expect_reset_ = true;

    client_->close();
  }
}

Http::CodecClient::Type
HttpHealthCheckerImpl::codecClientType(const envoy::type::CodecClientType& type) {
  switch (type) {
  case envoy::type::HTTP3:
    return Http::CodecClient::Type::HTTP3;
  case envoy::type::HTTP2:
    return Http::CodecClient::Type::HTTP2;
  case envoy::type::HTTP1:
    return Http::CodecClient::Type::HTTP1;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

Http::CodecClient*
ProdHttpHealthCheckerImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  return new Http::CodecClientProd(codec_client_type_, std::move(data.connection_),
                                   data.host_description_, dispatcher_);
}

TcpHealthCheckMatcher::MatchSegments TcpHealthCheckMatcher::loadProtoBytes(
    const Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload>& byte_array) {
  MatchSegments result;

  for (const auto& entry : byte_array) {
    const auto decoded = Hex::decode(entry.text());
    if (decoded.empty()) {
      throw EnvoyException(fmt::format("invalid hex string '{}'", entry.text()));
    }
    result.push_back(decoded);
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
                                           Runtime::RandomGenerator& random,
                                           HealthCheckEventLoggerPtr&& event_logger)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random, std::move(event_logger)),
      send_bytes_([&config] {
        Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload> send_repeated;
        if (!config.tcp_health_check().send().text().empty()) {
          send_repeated.Add()->CopyFrom(config.tcp_health_check().send());
        }
        return TcpHealthCheckMatcher::loadProtoBytes(send_repeated);
      }()),
      receive_bytes_(TcpHealthCheckMatcher::loadProtoBytes(config.tcp_health_check().receive())) {}

TcpHealthCheckerImpl::TcpActiveHealthCheckSession::~TcpActiveHealthCheckSession() {
  ASSERT(client_ == nullptr);
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onDeferredDelete() {
  if (client_) {
    expect_close_ = true;
    client_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onData(Buffer::Instance& data) {
  ENVOY_CONN_LOG(trace, "total pending buffer={}", *client_, data.length());
  if (TcpHealthCheckMatcher::match(parent_.receive_bytes_, data)) {
    ENVOY_CONN_LOG(trace, "healthcheck passed", *client_);
    data.drain(data.length());
    handleSuccess(false);
    if (!parent_.reuse_connection_) {
      expect_close_ = true;
      client_->close(Network::ConnectionCloseType::NoFlush);
    }
  } else {
    host_->setActiveHealthFailureType(Host::ActiveHealthFailureType::UNHEALTHY);
  }
}

void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (!expect_close_) {
      handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType::NETWORK);
    }
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
    expect_close_ = true;
    client_->close(Network::ConnectionCloseType::NoFlush);
    handleSuccess(false);
  }
}

// TODO(lilika) : Support connection pooling
void TcpHealthCheckerImpl::TcpActiveHealthCheckSession::onInterval() {
  if (!client_) {
    client_ = host_->createHealthCheckConnection(parent_.dispatcher_).connection_;
    session_callbacks_.reset(new TcpSessionCallbacks(*this));
    client_->addConnectionCallbacks(*session_callbacks_);
    client_->addReadFilter(session_callbacks_);

    expect_close_ = false;
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
  expect_close_ = true;
  host_->setActiveHealthFailureType(Host::ActiveHealthFailureType::TIMEOUT);
  client_->close(Network::ConnectionCloseType::NoFlush);
}

GrpcHealthCheckerImpl::GrpcHealthCheckerImpl(const Cluster& cluster,
                                             const envoy::api::v2::core::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Runtime::RandomGenerator& random,
                                             HealthCheckEventLoggerPtr&& event_logger)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random, std::move(event_logger)),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "grpc.health.v1.Health.Check")) {
  if (!config.grpc_health_check().service_name().empty()) {
    service_name_ = config.grpc_health_check().service_name();
  }

  if (!config.grpc_health_check().authority().empty()) {
    authority_value_ = config.grpc_health_check().authority();
  }
}

GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::GrpcActiveHealthCheckSession(
    GrpcHealthCheckerImpl& parent, const HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent) {}

GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::~GrpcActiveHealthCheckSession() {
  ASSERT(client_ == nullptr);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onDeferredDelete() {
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
    onRpcComplete(Grpc::Utility::httpToGrpcStatus(http_response_status), "non-200 HTTP response",
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

  const std::string& authority = parent_.authority_value_.has_value()
                                     ? parent_.authority_value_.value()
                                     : parent_.cluster_.info()->name();
  auto headers_message =
      Grpc::Common::prepareHeaders(authority, parent_.service_method_.service()->full_name(),
                                   parent_.service_method_.name(), absl::nullopt);
  headers_message->headers().insertUserAgent().value().setReference(
      Http::Headers::get().UserAgentValues.EnvoyHealthChecker);

  Router::FilterUtility::setUpstreamScheme(
      headers_message->headers(), host_->transportSocketFactory().implementsSecureTransport());

  request_encoder_->encodeHeaders(headers_message->headers(), false);

  grpc::health::v1::HealthCheckRequest request;
  if (parent_.service_name_.has_value()) {
    request.set_service(parent_.service_name_.value());
  }

  request_encoder_->encodeData(*Grpc::Common::serializeToGrpcFrame(request), true);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onResetStream(Http::StreamResetReason,
                                                                        absl::string_view) {
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
  handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType::NETWORK);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onGoAway() {
  ENVOY_CONN_LOG(debug, "connection going away health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));
  // Even if we have active health check probe, fail it on GOAWAY and schedule new one.
  if (request_encoder_) {
    handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType::NETWORK);
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
    handleSuccess(false);
  } else {
    handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType::ACTIVE);
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
      NOT_REACHED_GCOVR_EXCL_LINE;
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

std::ostream& operator<<(std::ostream& out, HealthState state) {
  switch (state) {
  case HealthState::Unhealthy:
    out << "Unhealthy";
    break;
  case HealthState::Healthy:
    out << "Healthy";
    break;
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, HealthTransition changed_state) {
  switch (changed_state) {
  case HealthTransition::Unchanged:
    out << "Unchanged";
    break;
  case HealthTransition::Changed:
    out << "Changed";
    break;
  case HealthTransition::ChangePending:
    out << "ChangePending";
    break;
  }
  return out;
}

} // namespace Upstream
} // namespace Envoy
