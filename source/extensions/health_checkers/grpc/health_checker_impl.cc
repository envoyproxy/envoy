#include "source/extensions/health_checkers/grpc/health_checker_impl.h"

#include <cstdint>
#include <iterator>
#include <memory>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/type/v3/range.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/macros.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/router/router.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/upstream/host_utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Upstream {
namespace {
const std::string& getHostname(const HostSharedPtr& host,
                               const absl::optional<std::string>& config_hostname,
                               const ClusterInfoConstSharedPtr& cluster) {
  if (config_hostname.has_value()) {
    return HealthCheckerFactory::getHostname(host, config_hostname.value(), cluster);
  }
  return HealthCheckerFactory::getHostname(host, EMPTY_STRING, cluster);
}
} // namespace

Upstream::HealthCheckerSharedPtr GrpcHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<ProdGrpcHealthCheckerImpl>(
      context.cluster(), config, context.mainThreadDispatcher(), context.runtime(),
      context.api().randomGenerator(), context.eventLogger());
}

REGISTER_FACTORY(GrpcHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

GrpcHealthCheckerImpl::GrpcHealthCheckerImpl(const Cluster& cluster,
                                             const envoy::config::core::v3::HealthCheck& config,
                                             Event::Dispatcher& dispatcher,
                                             Runtime::Loader& runtime,
                                             Random::RandomGenerator& random,
                                             HealthCheckEventLoggerPtr&& event_logger)
    : HealthCheckerImplBase(cluster, config, dispatcher, runtime, random, std::move(event_logger)),
      random_generator_(random),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "grpc.health.v1.Health.Check")),
      request_headers_parser_(THROW_OR_RETURN_VALUE(
          Router::HeaderParser::configure(config.grpc_health_check().initial_metadata()),
          Router::HeaderParserPtr)) {
  if (!config.grpc_health_check().service_name().empty()) {
    service_name_ = config.grpc_health_check().service_name();
  }

  if (!config.grpc_health_check().authority().empty()) {
    authority_value_ = config.grpc_health_check().authority();
  }
}

GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::GrpcActiveHealthCheckSession(
    GrpcHealthCheckerImpl& parent, const HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent),
      local_connection_info_provider_(std::make_shared<Network::ConnectionInfoSetterImpl>(
          Network::Utility::getCanonicalIpv4LoopbackAddress(),
          Network::Utility::getCanonicalIpv4LoopbackAddress())) {}

GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::~GrpcActiveHealthCheckSession() {
  ASSERT(client_ == nullptr);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onDeferredDelete() {
  if (client_) {
    // If there is an active request it will get reset, so make sure we ignore the reset.
    expect_reset_ = true;
    client_->close(Network::ConnectionCloseType::Abort);
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::decodeHeaders(
    Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
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
  if (!Grpc::Common::isGrpcResponseHeaders(*headers, end_stream)) {
    onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal, "not a gRPC request", false);
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
    onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal,
                  "gRPC protocol violation: unexpected stream end", true);
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::decodeData(Buffer::Instance& data,
                                                                     bool end_stream) {
  if (end_stream) {
    onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal,
                  "gRPC protocol violation: unexpected stream end", true);
    return;
  }
  // We should end up with only one frame here.
  std::vector<Grpc::Frame> decoded_frames;
  if (!decoder_.decode(data, decoded_frames).ok()) {
    onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal, "gRPC wire protocol decode error",
                  false);
    return;
  }
  for (auto& frame : decoded_frames) {
    if (frame.length_ > 0) {
      if (health_check_response_) {
        // grpc.health.v1.Health.Check is unary RPC, so only one message is allowed.
        onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal, "unexpected streaming", false);
        return;
      }
      health_check_response_ = std::make_unique<grpc::health::v1::HealthCheckResponse>();
      Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));

      if (frame.flags_ != Grpc::GRPC_FH_DEFAULT ||
          !health_check_response_->ParseFromZeroCopyStream(&stream)) {
        onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal,
                      "invalid grpc.health.v1 RPC payload", false);
        return;
      }
    }
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::decodeTrailers(
    Http::ResponseTrailerMapPtr&& trailers) {
  auto maybe_grpc_status = Grpc::Common::getGrpcStatus(*trailers);
  auto grpc_status =
      maybe_grpc_status
          ? maybe_grpc_status.value()
          : static_cast<Grpc::Status::GrpcStatus>(Grpc::Status::WellKnownGrpcStatus::Internal);
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
        host_->createHealthCheckConnection(parent_.dispatcher_, parent_.transportSocketOptions(),
                                           parent_.transportSocketMatchMetadata().get());
    client_ = parent_.createCodecClient(conn);
    client_->addConnectionCallbacks(connection_callback_impl_);
    client_->setCodecConnectionCallbacks(http_connection_callback_impl_);
  }

  request_encoder_ = &client_->newStream(*this);
  request_encoder_->getStream().addCallbacks(*this);

  const std::string& authority =
      getHostname(host_, parent_.authority_value_, parent_.cluster_.info());
  auto headers_message =
      Grpc::Common::prepareHeaders(authority, parent_.service_method_.service()->full_name(),
                                   parent_.service_method_.name(), absl::nullopt);
  headers_message->headers().setReferenceUserAgent(
      Http::Headers::get().UserAgentValues.EnvoyHealthChecker);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, parent_.dispatcher_.timeSource(),
                                         local_connection_info_provider_,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
  stream_info.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  stream_info.upstreamInfo()->setUpstreamHost(host_);
  parent_.request_headers_parser_->evaluateHeaders(headers_message->headers(), stream_info);

  Grpc::Common::toGrpcTimeout(parent_.timeout_, headers_message->headers());

  Router::FilterUtility::setUpstreamScheme(
      headers_message->headers(),
      // Here there is no downstream connection so scheme will be based on
      // upstream crypto
      false, host_->transportSocketFactory().implementsSecureTransport(), true);

  auto status = request_encoder_->encodeHeaders(headers_message->headers(), false);
  // Encoding will only fail if required headers are missing.
  ASSERT(status.ok());

  grpc::health::v1::HealthCheckRequest request;
  if (parent_.service_name_.has_value()) {
    request.set_service(parent_.service_name_.value());
  }

  request_encoder_->encodeData(*Grpc::Common::serializeToGrpcFrame(request), true);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onResetStream(Http::StreamResetReason,
                                                                        absl::string_view) {
  const bool expected_reset = expect_reset_;
  const bool goaway = received_no_error_goaway_;
  resetState();

  if (expected_reset) {
    // Stream reset was initiated by us (bogus gRPC response, timeout or cluster host is going
    // away). In these cases health check failure has already been reported and a GOAWAY (if any)
    // has already been handled, so just return.
    return;
  }

  ENVOY_CONN_LOG(debug, "connection/stream error health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));

  if (goaway || !parent_.reuse_connection_) {
    // Stream reset was unexpected, so we haven't closed the connection
    // yet in response to a GOAWAY or due to disabled connection reuse.
    client_->close(Network::ConnectionCloseType::Abort);
  }

  // TODO(baranov1ch): according to all HTTP standards, we should check if reason is one of
  // Http::StreamResetReason::RemoteRefusedStreamReset (which may mean GOAWAY),
  // Http::StreamResetReason::RemoteReset or Http::StreamResetReason::ConnectionTermination (both
  // mean connection close), check if connection is not fresh (was used for at least 1 request)
  // and silently retry request on the fresh connection. This is also true for HTTP/1.1 healthcheck.
  handleFailure(envoy::data::core::v3::NETWORK);
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onGoAway(
    Http::GoAwayErrorCode error_code) {
  ENVOY_CONN_LOG(debug, "connection going away health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));
  // If we have an active health check probe and receive a GOAWAY indicating
  // graceful shutdown, allow the probe to complete before closing the connection.
  // The connection will be closed when the active check completes or another
  // terminal condition occurs, such as a timeout or stream reset.
  if (request_encoder_ && error_code == Http::GoAwayErrorCode::NoError) {
    received_no_error_goaway_ = true;
    return;
  }

  // Even if we have active health check probe, fail it on GOAWAY and schedule new one.
  if (request_encoder_) {
    handleFailure(envoy::data::core::v3::NETWORK);
    // request_encoder_ can already be destroyed if the host was removed during the failure callback
    // above.
    if (request_encoder_ != nullptr) {
      expect_reset_ = true;
      request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
    }
  }
  // client_ can already be destroyed if the host was removed during the failure callback above.
  if (client_ != nullptr) {
    client_->close(Network::ConnectionCloseType::Abort);
  }
}

bool GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::isHealthCheckSucceeded(
    Grpc::Status::GrpcStatus grpc_status) const {
  if (grpc_status != Grpc::Status::WellKnownGrpcStatus::Ok) {
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
    handleFailure(envoy::data::core::v3::ACTIVE);
  }

  // Read the value as we may call resetState() and clear it.
  const bool goaway = received_no_error_goaway_;

  // |end_stream| will be false if we decided to stop healthcheck before HTTP stream has ended -
  // invalid gRPC payload, unexpected message stream or wrong content-type.
  if (end_stream) {
    resetState();
  } else {
    // request_encoder_ can already be destroyed if the host was removed during the failure callback
    // above.
    if (request_encoder_ != nullptr) {
      // resetState() will be called by onResetStream().
      expect_reset_ = true;
      request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
    }
  }

  // client_ can already be destroyed if the host was removed during the failure callback above.
  if (client_ != nullptr && (!parent_.reuse_connection_ || goaway)) {
    client_->close(Network::ConnectionCloseType::Abort);
  }
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::resetState() {
  expect_reset_ = false;
  request_encoder_ = nullptr;
  decoder_ = Grpc::Decoder();
  health_check_response_.reset();
  received_no_error_goaway_ = false;
}

void GrpcHealthCheckerImpl::GrpcActiveHealthCheckSession::onTimeout() {
  ENVOY_CONN_LOG(debug, "connection/stream timeout health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));
  expect_reset_ = true;
  if (received_no_error_goaway_ || !parent_.reuse_connection_) {
    client_->close(Network::ConnectionCloseType::Abort);
  } else {
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }
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
    case grpc::health::v1::HealthCheckResponse::SERVICE_UNKNOWN:
      service_status = "service_unknown";
      break;
    default:
      service_status = "unknown_healthcheck_response";
      break;
    }
  }
  std::string grpc_status_message;
  if (grpc_status != Grpc::Status::WellKnownGrpcStatus::Ok && !grpc_message.empty()) {
    grpc_status_message = fmt::format("{} ({})", grpc_status, grpc_message);
  } else {
    grpc_status_message = absl::StrCat("", grpc_status);
  }

  ENVOY_CONN_LOG(debug, "hc grpc_status={} service_status={} health_flags={}", *client_,
                 grpc_status_message, service_status, HostUtility::healthFlagsToString(*host_));
}

Http::CodecClientPtr
ProdGrpcHealthCheckerImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  return std::make_unique<Http::CodecClientProd>(
      Http::CodecType::HTTP2, std::move(data.connection_), data.host_description_, dispatcher_,
      random_generator_, transportSocketOptions());
}

} // namespace Upstream
} // namespace Envoy
