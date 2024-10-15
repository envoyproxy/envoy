#include "source/extensions/health_checkers/http/health_checker_impl.h"

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

envoy::config::core::v3::RequestMethod
getMethod(const envoy::config::core::v3::RequestMethod config_method) {
  if (config_method == envoy::config::core::v3::METHOD_UNSPECIFIED) {
    return envoy::config::core::v3::GET;
  }

  return config_method;
}

} // namespace

Upstream::HealthCheckerSharedPtr HttpHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<ProdHttpHealthCheckerImpl>(context.cluster(), config, context,
                                                     context.eventLogger());
}

REGISTER_FACTORY(HttpHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

HttpHealthCheckerImpl::HttpHealthCheckerImpl(
    const Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context,
    HealthCheckEventLoggerPtr&& event_logger)
    : HealthCheckerImplBase(cluster, config, context.mainThreadDispatcher(), context.runtime(),
                            context.api().randomGenerator(), std::move(event_logger)),
      path_(config.http_health_check().path()), host_value_(config.http_health_check().host()),
      method_(getMethod(config.http_health_check().method())),
      response_buffer_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config.http_health_check(), response_buffer_size, kDefaultMaxBytesInBuffer)),
      request_headers_parser_(THROW_OR_RETURN_VALUE(
          Router::HeaderParser::configure(config.http_health_check().request_headers_to_add(),
                                          config.http_health_check().request_headers_to_remove()),
          Router::HeaderParserPtr)),
      http_status_checker_(config.http_health_check().expected_statuses(),
                           config.http_health_check().retriable_statuses(),
                           static_cast<uint64_t>(Http::Code::OK)),
      codec_client_type_(codecClientType(config.http_health_check().codec_client_type())),
      random_generator_(context.api().randomGenerator()) {
  // TODO(boteng): introduce additional validation for the authority and path headers
  // based on the default UHV when it is available.
  auto bytes_or_error = PayloadMatcher::loadProtoBytes(config.http_health_check().receive());
  THROW_IF_NOT_OK_REF(bytes_or_error.status());
  receive_bytes_ = bytes_or_error.value();
  if (config.http_health_check().has_service_name_matcher()) {
    service_name_matcher_.emplace(config.http_health_check().service_name_matcher(),
                                  context.serverFactoryContext());
  }

  if (response_buffer_size_ != 0 && !receive_bytes_.empty()) {
    uint64_t total = 0;
    for (auto const& bytes : receive_bytes_) {
      total += bytes.size();
    }
    if (total > response_buffer_size_) {
      throw EnvoyException(fmt::format(
          "The expected response length '{}' is over than http health response buffer size '{}'",
          total, response_buffer_size_));
    }
  }
}

HttpHealthCheckerImpl::HttpStatusChecker::HttpStatusChecker(
    const Protobuf::RepeatedPtrField<envoy::type::v3::Int64Range>& expected_statuses,
    const Protobuf::RepeatedPtrField<envoy::type::v3::Int64Range>& retriable_statuses,
    uint64_t default_expected_status) {
  for (const auto& status_range : expected_statuses) {
    const auto start = static_cast<uint64_t>(status_range.start());
    const auto end = static_cast<uint64_t>(status_range.end());

    validateRange(start, end, "expected");

    expected_ranges_.emplace_back(std::make_pair(start, end));
  }

  if (expected_ranges_.empty()) {
    expected_ranges_.emplace_back(
        std::make_pair(default_expected_status, default_expected_status + 1));
  }

  for (const auto& status_range : retriable_statuses) {
    const auto start = static_cast<uint64_t>(status_range.start());
    const auto end = static_cast<uint64_t>(status_range.end());

    validateRange(start, end, "retriable");

    retriable_ranges_.emplace_back(std::make_pair(start, end));
  }
}

void HttpHealthCheckerImpl::HttpStatusChecker::validateRange(uint64_t start, uint64_t end,
                                                             absl::string_view range_type) {
  if (start >= end) {
    throw EnvoyException(fmt::format("Invalid http {} status range: expecting start < "
                                     "end, but found start={} and end={}",
                                     range_type, start, end));
  }

  if (start < 100) {
    throw EnvoyException(
        fmt::format("Invalid http {} status range: expecting start >= 100, but found start={}",
                    range_type, start));
  }

  if (end > 600) {
    throw EnvoyException(fmt::format(
        "Invalid http {} status range: expecting end <= 600, but found end={}", range_type, end));
  }
}

bool HttpHealthCheckerImpl::HttpStatusChecker::inRetriableRanges(uint64_t http_status) const {
  return inRanges(http_status, retriable_ranges_);
}

bool HttpHealthCheckerImpl::HttpStatusChecker::inExpectedRanges(uint64_t http_status) const {
  return inRanges(http_status, expected_ranges_);
}

bool HttpHealthCheckerImpl::HttpStatusChecker::inRanges(
    uint64_t http_status, const std::vector<std::pair<uint64_t, uint64_t>>& ranges) {
  for (const auto& range : ranges) {
    if (http_status >= range.first && http_status < range.second) {
      return true;
    }
  }

  return false;
}

Http::Protocol codecClientTypeToProtocol(Http::CodecType codec_client_type) {
  switch (codec_client_type) {
  case Http::CodecType::HTTP1:
    return Http::Protocol::Http11;
  case Http::CodecType::HTTP2:
    return Http::Protocol::Http2;
  case Http::CodecType::HTTP3:
    return Http::Protocol::Http3;
  }
  PANIC_DUE_TO_CORRUPT_ENUM
}

Http::Protocol HttpHealthCheckerImpl::protocol() const {
  return codecClientTypeToProtocol(codec_client_type_);
}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::HttpActiveHealthCheckSession(
    HttpHealthCheckerImpl& parent, const HostSharedPtr& host)
    : ActiveHealthCheckSession(parent, host), parent_(parent),
      response_body_(std::make_unique<Buffer::OwnedImpl>()),
      hostname_(
          HealthCheckerFactory::getHostname(host, parent_.host_value_, parent_.cluster_.info())),
      local_connection_info_provider_(std::make_shared<Network::ConnectionInfoSetterImpl>(
          Network::Utility::getCanonicalIpv4LoopbackAddress(),
          Network::Utility::getCanonicalIpv4LoopbackAddress())),
      protocol_(codecClientTypeToProtocol(parent_.codec_client_type_)), expect_reset_(false),
      reuse_connection_(false), request_in_flight_(false) {}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::~HttpActiveHealthCheckSession() {
  ASSERT(client_ == nullptr);
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onDeferredDelete() {
  if (client_) {
    // If there is an active request it will get reset, so make sure we ignore the reset.
    expect_reset_ = true;
    client_->close(Network::ConnectionCloseType::Abort);
  }
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::decodeHeaders(
    Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  ASSERT(!response_headers_);
  response_headers_ = std::move(headers);
  if (end_stream) {
    onResponseComplete();
  }
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::decodeData(Buffer::Instance& data,
                                                                     bool end_stream) {
  if (parent_.response_buffer_size_ != 0) {
    if (!parent_.receive_bytes_.empty() &&
        response_body_->length() < parent_.response_buffer_size_) {
      response_body_->move(data, parent_.response_buffer_size_ - response_body_->length());
    }
  } else {
    if (!parent_.receive_bytes_.empty()) {
      response_body_->move(data, data.length());
    }
  }

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
    response_body_->drain(response_body_->length());
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

// TODO(lilika) : Support connection pooling
void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onInterval() {
  if (!client_) {
    Upstream::Host::CreateConnectionData conn =
        host_->createHealthCheckConnection(parent_.dispatcher_, parent_.transportSocketOptions(),
                                           parent_.transportSocketMatchMetadata().get());
    client_.reset(parent_.createCodecClient(conn));
    client_->addConnectionCallbacks(connection_callback_impl_);
    client_->setCodecConnectionCallbacks(http_connection_callback_impl_);
    expect_reset_ = false;
    reuse_connection_ = parent_.reuse_connection_;
  }

  Http::RequestEncoder* request_encoder = &client_->newStream(*this);
  request_encoder->getStream().addCallbacks(*this);
  request_in_flight_ = true;

  const auto request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
      {{Http::Headers::get().Method, envoy::config::core::v3::RequestMethod_Name(parent_.method_)},
       {Http::Headers::get().Host, hostname_},
       {Http::Headers::get().Path, parent_.path_},
       {Http::Headers::get().UserAgent, Http::Headers::get().UserAgentValues.EnvoyHealthChecker}});
  Router::FilterUtility::setUpstreamScheme(
      *request_headers,
      // Here there is no downstream connection so scheme will be based on
      // upstream crypto
      false, host_->transportSocketFactory().implementsSecureTransport(), true);
  StreamInfo::StreamInfoImpl stream_info(protocol_, parent_.dispatcher_.timeSource(),
                                         local_connection_info_provider_,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
  stream_info.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  stream_info.upstreamInfo()->setUpstreamHost(host_);
  parent_.request_headers_parser_->evaluateHeaders(*request_headers, stream_info);
  auto status = request_encoder->encodeHeaders(*request_headers, true);
  // Encoding will only fail if required request headers are missing.
  ASSERT(status.ok());
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onResetStream(Http::StreamResetReason,
                                                                        absl::string_view) {
  request_in_flight_ = false;
  ENVOY_CONN_LOG(debug, "connection/stream error health_flags={}", *client_,
                 HostUtility::healthFlagsToString(*host_));
  if (expect_reset_) {
    return;
  }

  if (client_ && !reuse_connection_) {
    client_->close(Network::ConnectionCloseType::Abort);
  }

  handleFailure(envoy::data::core::v3::NETWORK);
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onGoAway(
    Http::GoAwayErrorCode error_code) {
  ENVOY_CONN_LOG(debug, "connection going away goaway_code={}, health_flags={}", *client_,
                 static_cast<int>(error_code), HostUtility::healthFlagsToString(*host_));

  if (request_in_flight_ && error_code == Http::GoAwayErrorCode::NoError) {
    // The server is starting a graceful shutdown. Allow the in flight request
    // to finish without treating this as a health check error, and then
    // reconnect.
    reuse_connection_ = false;
    return;
  }

  if (request_in_flight_) {
    // Record this as a failed health check.
    handleFailure(envoy::data::core::v3::NETWORK);
  }

  if (client_) {
    expect_reset_ = true;
    client_->close(Network::ConnectionCloseType::Abort);
  }
}

HttpHealthCheckerImpl::HttpActiveHealthCheckSession::HealthCheckResult
HttpHealthCheckerImpl::HttpActiveHealthCheckSession::healthCheckResult() {
  const uint64_t response_code = Http::Utility::getResponseStatus(*response_headers_);
  ENVOY_CONN_LOG(debug, "hc response_code={} health_flags={}", *client_, response_code,
                 HostUtility::healthFlagsToString(*host_));

  if (!parent_.receive_bytes_.empty()) {
    // If the expected response is set, check the first 1024 bytes of actual response if contains
    // the expected response.
    if (!PayloadMatcher::match(parent_.receive_bytes_, *response_body_)) {
      if (response_headers_->EnvoyImmediateHealthCheckFail() != nullptr) {
        host_->healthFlagSet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);
      }
      return HealthCheckResult::Failed;
    }
    ENVOY_CONN_LOG(debug, "hc http response body healthcheck passed", *client_);
  }

  if (!parent_.http_status_checker_.inExpectedRanges(response_code)) {
    // If the HTTP response code would indicate failure AND the immediate health check
    // failure header is set, exclude the host from LB.
    // TODO(mattklein123): We could consider doing this check for any HTTP response code, but this
    // seems like the least surprising behavior and we could consider relaxing this in the future.
    // TODO(mattklein123): This will not force a host set rebuild of the host was already failed.
    // This is something we could do in the future but seems unnecessary right now.
    if (response_headers_->EnvoyImmediateHealthCheckFail() != nullptr) {
      host_->healthFlagSet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);
    }

    if (parent_.http_status_checker_.inRetriableRanges(response_code)) {
      return HealthCheckResult::Retriable;
    } else {
      return HealthCheckResult::Failed;
    }
  }

  const auto degraded = response_headers_->EnvoyDegraded() != nullptr;

  if (parent_.service_name_matcher_.has_value() &&
      parent_.runtime_.snapshot().featureEnabled("health_check.verify_cluster", 100UL)) {
    parent_.stats_.verify_cluster_.inc();
    std::string service_cluster_healthchecked =
        response_headers_->EnvoyUpstreamHealthCheckedCluster()
            ? std::string(response_headers_->getEnvoyUpstreamHealthCheckedClusterValue())
            : EMPTY_STRING;
    if (parent_.service_name_matcher_->match(service_cluster_healthchecked)) {
      return degraded ? HealthCheckResult::Degraded : HealthCheckResult::Succeeded;
    } else {
      return HealthCheckResult::Failed;
    }
  }

  return degraded ? HealthCheckResult::Degraded : HealthCheckResult::Succeeded;
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onResponseComplete() {
  request_in_flight_ = false;

  switch (healthCheckResult()) {
  case HealthCheckResult::Succeeded:
    handleSuccess(false);
    break;
  case HealthCheckResult::Degraded:
    handleSuccess(true);
    break;
  case HealthCheckResult::Failed:
    handleFailure(envoy::data::core::v3::ACTIVE, /*retriable=*/false);
    break;
  case HealthCheckResult::Retriable:
    handleFailure(envoy::data::core::v3::ACTIVE, /*retriable=*/true);
    break;
  }

  if (shouldClose()) {
    client_->close(Network::ConnectionCloseType::Abort);
  }

  response_headers_.reset();
  response_body_->drain(response_body_->length());
}

// It is possible for this session to have been deferred destroyed inline in handleFailure()
// above so make sure we still have a connection that we might need to close.
bool HttpHealthCheckerImpl::HttpActiveHealthCheckSession::shouldClose() const {
  if (client_ == nullptr) {
    return false;
  }

  if (!reuse_connection_) {
    return true;
  }

  return Http::HeaderUtility::shouldCloseConnection(client_->protocol(), *response_headers_);
}

void HttpHealthCheckerImpl::HttpActiveHealthCheckSession::onTimeout() {
  request_in_flight_ = false;
  if (client_) {
    ENVOY_CONN_LOG(debug, "connection/stream timeout health_flags={}", *client_,
                   HostUtility::healthFlagsToString(*host_));

    // If there is an active request it will get reset, so make sure we ignore the reset.
    expect_reset_ = true;

    client_->close(Network::ConnectionCloseType::Abort);
  }
}

Http::CodecType
HttpHealthCheckerImpl::codecClientType(const envoy::type::v3::CodecClientType& type) {
  switch (type) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::type::v3::HTTP3:
    return Http::CodecType::HTTP3;
  case envoy::type::v3::HTTP2:
    return Http::CodecType::HTTP2;
  case envoy::type::v3::HTTP1:
    return Http::CodecType::HTTP1;
  }
  PANIC_DUE_TO_CORRUPT_ENUM
}

Http::CodecClient*
ProdHttpHealthCheckerImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  return new Http::CodecClientProd(codec_client_type_, std::move(data.connection_),
                                   data.host_description_, dispatcher_, random_generator_,
                                   transportSocketOptions());
}

} // namespace Upstream
} // namespace Envoy
