#include "source/extensions/load_balancing_policies/common/orca_oob_manager.h"

#include "envoy/http/codes.h"
#include "envoy/upstream/upstream.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "xds/service/orca/v3/orca.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace Common {

namespace {
constexpr absl::string_view kOrcaOobStatsPrefix = "lb_orca_oob.";
constexpr uint64_t kOobBackoffBaseIntervalMs = 1000;
constexpr uint64_t kOobBackoffMaxIntervalMs = 30000;
constexpr absl::string_view kOrcaOobServiceFullName = "xds.service.orca.v3.OpenRcaService";
constexpr absl::string_view kStreamCoreMetricsMethod = "StreamCoreMetrics";
// Multiplier on reporting_period_ used to size the per-session inactivity watchdog.
// 3x gives the server two missed reports of slack before we treat the stream as
// stalled and reconnect via handleTransientFailure.
constexpr uint64_t kInactivityWatchdogMultiplier = 3;

// Max allowed size of a single gRPC frame accepted on an OOB OrcaLoadReport stream.
constexpr uint32_t kMaxOrcaReportFrameBytes = 64 * 1024;
} // namespace

OrcaOobManager::OrcaOobManager(std::chrono::milliseconds reporting_period,
                               const Upstream::PrioritySet& priority_set,
                               Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                               Stats::Scope& stats_scope,
                               OrcaLoadReportHandlerSharedPtr report_handler)
    : dispatcher_(dispatcher), random_(random), reporting_period_(reporting_period),
      priority_set_(priority_set), report_handler_(std::move(report_handler)),
      oob_stats_(generateOrcaOobStats(stats_scope)) {}

OrcaOobManager::~OrcaOobManager() {
  for (auto& [host, session] : oob_sessions_) {
    session->disarm();
    dispatcher_.deferredDelete(std::move(session));
  }
  oob_sessions_.clear();
  oob_stats_.active_sessions_.set(0);
}

absl::Status OrcaOobManager::initialize() {
  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    onHostsAdded(host_set->hosts());
  }
  member_update_cb_ = priority_set_.addMemberUpdateCb(
      [this](const Upstream::HostVector& hosts_added,
             const Upstream::HostVector& hosts_removed) -> absl::Status {
        onHostsRemoved(hosts_removed);
        onHostsAdded(hosts_added);
        return absl::OkStatus();
      });
  return absl::OkStatus();
}

OrcaOobStats OrcaOobManager::generateOrcaOobStats(Stats::Scope& scope) {
  return {ALL_ORCA_OOB_STATS(POOL_COUNTER_PREFIX(scope, kOrcaOobStatsPrefix),
                             POOL_GAUGE_PREFIX(scope, kOrcaOobStatsPrefix))};
}

void OrcaOobManager::onHostsAdded(const Upstream::HostVector& hosts) {
  const size_t prior_size = oob_sessions_.size();
  for (const Upstream::HostSharedPtr& host : hosts) {
    auto [it, inserted] = oob_sessions_.try_emplace(host, nullptr);
    if (!inserted) {
      continue;
    }
    const uint64_t period_ms = reporting_period_.count();
    const std::chrono::milliseconds initial_delay(period_ms == 0 ? 0
                                                                 : random_.random() % period_ms);
    it->second = std::make_unique<OobSession>(*this, host, initial_delay);
  }
  if (oob_sessions_.size() != prior_size) {
    oob_stats_.active_sessions_.set(oob_sessions_.size());
  }
}

void OrcaOobManager::onHostsRemoved(const Upstream::HostVector& hosts) {
  const size_t prior_size = oob_sessions_.size();
  for (const Upstream::HostSharedPtr& host : hosts) {
    auto it = oob_sessions_.find(host);
    if (it == oob_sessions_.end()) {
      continue;
    }
    it->second->disarm();
    dispatcher_.deferredDelete(std::move(it->second));
    oob_sessions_.erase(it);
  }
  if (oob_sessions_.size() != prior_size) {
    oob_stats_.active_sessions_.set(oob_sessions_.size());
  }
}

void OrcaOobManager::onSessionTerminated(OobSession* session) {
  auto it = oob_sessions_.find(session->host());
  ASSERT(it != oob_sessions_.end() && it->second.get() == session);
  dispatcher_.deferredDelete(std::move(it->second));
  oob_sessions_.erase(it);
  oob_stats_.active_sessions_.set(oob_sessions_.size());
}

OrcaOobManager::OobSession::OobSession(OrcaOobManager& parent, Upstream::HostConstSharedPtr host,
                                       std::chrono::milliseconds initial_delay)
    : parent_(parent), host_(std::move(host)),
      backoff_(std::make_unique<JitteredExponentialBackOffStrategy>(
          kOobBackoffBaseIntervalMs, kOobBackoffMaxIntervalMs, parent_.random_)) {
  attempt_timer_ = parent_.dispatcher_.createTimer([this]() { connectAndStream(); });
  attempt_timer_->enableTimer(initial_delay);
  inactivity_timer_ =
      parent_.dispatcher_.createTimer([this]() { handleTransientFailure("inactivity timeout"); });
}

OrcaOobManager::OobSession::~OobSession() { ASSERT(codec_client_ == nullptr); }

void OrcaOobManager::OobSession::disarm() {
  attempt_timer_->disableTimer();
  inactivity_timer_->disableTimer();
  if (codec_client_ != nullptr) {
    tearDownCodec();
  }
}

void OrcaOobManager::OobSession::tearDownCodec() {
  ASSERT(codec_client_ != nullptr);
  expect_reset_ = true;
  auto client = std::move(codec_client_);
  client->close(Network::ConnectionCloseType::Abort);
  parent_.dispatcher_.deferredDelete(std::move(client));
  request_encoder_ = nullptr;
}

void OrcaOobManager::OobSession::decodeHeaders(Http::ResponseHeaderMapPtr&& headers,
                                               bool end_stream) {
  if (!Grpc::Common::isGrpcResponseHeaders(*headers, end_stream)) {
    const uint64_t http_status = Http::Utility::getResponseStatusOrNullopt(*headers).value_or(
        static_cast<uint64_t>(Http::Code::InternalServerError));
    const auto http_grpc_status = Grpc::Utility::httpToGrpcStatus(http_status);
    const auto status = end_stream
                            ? Grpc::Common::getGrpcStatus(*headers, /*allow_user_status=*/true)
                                  .value_or(http_grpc_status)
                            : http_grpc_status;
    onRpcComplete(status, "non-grpc response", end_stream);
    return;
  }
  if (end_stream) {
    const auto grpc_status = Grpc::Common::getGrpcStatus(*headers, /*allow_user_status=*/true);
    onRpcComplete(grpc_status.value_or(Grpc::Status::WellKnownGrpcStatus::Unknown), "trailers-only",
                  /*end_stream=*/true);
    return;
  }
}

void OrcaOobManager::OobSession::decodeData(Buffer::Instance& data, bool end_stream) {
  std::vector<Grpc::Frame> frames;
  if (!decoder_.decode(data, frames).ok()) {
    onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal, "gRPC frame decode error",
                  end_stream);
    return;
  }
  for (auto& frame : frames) {
    if (frame.flags_ != Grpc::GRPC_FH_DEFAULT) {
      onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal,
                    absl::StrCat("invalid frame flags=", static_cast<uint32_t>(frame.flags_)),
                    end_stream);
      return;
    }
    if (frame.length_ == 0) {
      continue;
    }
    xds::data::orca::v3::OrcaLoadReport report;
    Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));
    if (!report.ParseFromZeroCopyStream(&stream)) {
      onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Internal, "invalid OrcaLoadReport payload",
                    end_stream);
      return;
    }
    onReport(report);
  }
  if (end_stream) {
    onRpcComplete(Grpc::Status::WellKnownGrpcStatus::Unknown,
                  "server ended stream without trailers", /*end_stream=*/true);
    return;
  }
  if (received_no_error_goaway_) {
    handleTransientFailure("graceful goaway, reconnecting");
  }
}

void OrcaOobManager::OobSession::decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  const auto status = Grpc::Common::getGrpcStatus(*trailers, /*allow_user_status=*/true);
  onRpcComplete(status.value_or(Grpc::Status::WellKnownGrpcStatus::Unknown),
                Grpc::Common::getGrpcMessage(*trailers),
                /*end_stream=*/true);
}

void OrcaOobManager::OobSession::onResetStream(Http::StreamResetReason reason, absl::string_view) {
  const bool expected_reset = expect_reset_;
  resetState();
  if (expected_reset) {
    return;
  }
  handleTransientFailure(
      absl::StrCat("stream reset: ", Http::Utility::resetReasonToString(reason)));
}

void OrcaOobManager::OobSession::onGoAway(Http::GoAwayErrorCode error_code) {
  if (error_code == Http::GoAwayErrorCode::NoError) {
    received_no_error_goaway_ = true;
    return;
  }
  handleTransientFailure(absl::StrCat("goaway error code=", static_cast<uint32_t>(error_code)));
}

void OrcaOobManager::OobSession::connectAndStream() {
  ASSERT(codec_client_ == nullptr);
  resetState();

  Upstream::Host::CreateConnectionData connection_data = host_->createOrcaReportingConnection(
      parent_.dispatcher_, /*transport_socket_options=*/nullptr, /*metadata=*/nullptr);
  codec_client_ = parent_.createCodecClient(connection_data);
  if (codec_client_ == nullptr) {
    parent_.oob_stats_.stream_failures_.inc();
    ENVOY_LOG(debug, "ORCA OOB transient failure for {}: createCodecClient returned null",
              host_->address()->asString());
    attempt_timer_->enableTimer(std::chrono::milliseconds(backoff_->nextBackOffMs()));
    return;
  }
  codec_client_->addConnectionCallbacks(connection_callback_impl_);
  codec_client_->setCodecConnectionCallbacks(*this);
  request_encoder_ = &codec_client_->newStream(*this);
  request_encoder_->getStream().addCallbacks(*this);

  auto headers_message =
      Grpc::Common::prepareHeaders(authority(), std::string(kOrcaOobServiceFullName),
                                   std::string(kStreamCoreMetricsMethod), std::nullopt);
  headers_message->headers().setReferenceScheme(
      host_->transportSocketFactory().implementsSecureTransport()
          ? Http::Headers::get().SchemeValues.Https
          : Http::Headers::get().SchemeValues.Http);

  const auto status =
      request_encoder_->encodeHeaders(headers_message->headers(), /*end_stream=*/false);
  if (!status.ok()) {
    handleTransientFailure(absl::StrCat("encodeHeaders failed: ", status.message()));
    return;
  }

  xds::service::orca::v3::OrcaLoadReportRequest request;
  if (parent_.reporting_period_.count() > 0) {
    *request.mutable_report_interval() =
        Protobuf::util::TimeUtil::MillisecondsToDuration(parent_.reporting_period_.count());
  }

  request_encoder_->encodeData(*Grpc::Common::serializeToGrpcFrame(request), /*end_stream=*/true);

  if (parent_.reporting_period_.count() > 0) {
    inactivity_timer_->enableTimer(parent_.reporting_period_ * kInactivityWatchdogMultiplier);
  }
}
void OrcaOobManager::OobSession::onConnectionEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected ||
      event == Network::ConnectionEvent::ConnectedZeroRtt) {
    return;
  }
  if (codec_client_ != nullptr) {
    handleTransientFailure("connection closed");
  }
}

void OrcaOobManager::OobSession::handleTransientFailure(absl::string_view reason) {
  if (codec_client_ == nullptr) {
    return; // codec already torn down by another path.
  }
  parent_.oob_stats_.stream_failures_.inc();
  ENVOY_CONN_LOG(debug, "ORCA OOB transient failure for {}: {}", *codec_client_,
                 host_->address()->asString(), reason);
  inactivity_timer_->disableTimer();
  tearDownCodec();
  resetState();
  attempt_timer_->enableTimer(std::chrono::milliseconds(backoff_->nextBackOffMs()));
}

void OrcaOobManager::OobSession::handleTerminal(Grpc::Status::GrpcStatus status,
                                                absl::string_view reason) {
  if (codec_client_ == nullptr) {
    return; // codec already torn down by another path.
  }
  parent_.oob_stats_.stream_terminated_.inc();
  ENVOY_CONN_LOG(info, "ORCA OOB terminal for {}: status={} {}", *codec_client_,
                 host_->address()->asString(), static_cast<int>(status), reason);
  inactivity_timer_->disableTimer();
  tearDownCodec();
  attempt_timer_->disableTimer();
  parent_.onSessionTerminated(this);
}

void OrcaOobManager::OobSession::onRpcComplete(Grpc::Status::GrpcStatus status,
                                               absl::string_view message, bool end_stream) {
  if (!end_stream && request_encoder_ != nullptr) {
    expect_reset_ = true;
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }
  if (status == Grpc::Status::WellKnownGrpcStatus::Unimplemented) {
    handleTerminal(status, message);
  } else {
    handleTransientFailure(
        absl::StrCat("rpc complete status=", static_cast<int>(status), " ", message));
  }
}

void OrcaOobManager::OobSession::onReport(const xds::data::orca::v3::OrcaLoadReport& report) {
  parent_.oob_stats_.reports_received_.inc();
  backoff_->reset();
  if (parent_.reporting_period_.count() > 0) {
    inactivity_timer_->enableTimer(parent_.reporting_period_ * kInactivityWatchdogMultiplier);
  }
  auto data_opt = host_->typedLbPolicyData<OrcaHostLbPolicyData>();
  if (!data_opt.has_value()) {
    parent_.oob_stats_.report_errors_.inc();
    return;
  }
  const absl::Status status =
      parent_.report_handler_->updateClientSideDataFromOrcaLoadReport(report, *data_opt);
  if (!status.ok()) {
    parent_.oob_stats_.report_errors_.inc();
  }
}

void OrcaOobManager::OobSession::resetState() {
  expect_reset_ = false;
  request_encoder_ = nullptr;
  decoder_ = Grpc::Decoder();
  decoder_.setMaxFrameLength(kMaxOrcaReportFrameBytes);
  received_no_error_goaway_ = false;
}

std::string OrcaOobManager::OobSession::authority() const {
  if (!host_->hostname().empty()) {
    return std::string(host_->hostname());
  }
  // For IP addresses, use the full address string which includes brackets for IPv6 and the port.
  if (host_->address()->ip() != nullptr) {
    return host_->address()->asString();
  }
  // Terminal fallback for non-IP addresses (e.g., UDS, Pipe) avoids using a
  // socket path as :authority.
  return host_->cluster().name();
}

Http::CodecClientPtr
ProdOrcaOobManager::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  return std::make_unique<Http::CodecClientProd>(
      Http::CodecType::HTTP2, std::move(data.connection_), data.host_description_, dispatcher_,
      random_, /*transport_socket_options=*/nullptr);
}

} // namespace Common
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
