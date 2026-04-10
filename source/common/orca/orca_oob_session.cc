#include "source/common/orca/orca_oob_session.h"

#include <chrono>
#include <memory>

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/router.h"

#include "xds/data/orca/v3/orca_load_report.pb.h"
#include "xds/service/orca/v3/orca.pb.h"

namespace Envoy {
namespace Orca {

namespace {
// Backoff for OOB gRPC stream reconnect.
constexpr uint64_t InitialBackoffMs = 1000; // 1s
constexpr uint64_t MaxBackoffMs = 60000;    // 60s
// Defense against a malicious/buggy upstream; OrcaLoadReport is small in
// practice (few KB with generous named_metrics maps), 4MB is well above the
// worst realistic case while still bounding memory.
constexpr uint32_t MaxOrcaFrameBytes = 4 * 1024 * 1024;
} // namespace

OrcaOobSession::OrcaOobSession(Upstream::HostSharedPtr host, Event::Dispatcher& dispatcher,
                               Random::RandomGenerator& random,
                               std::chrono::milliseconds reporting_period,
                               OrcaOobCallbacks& callbacks, OrcaOobStats& stats)
    : host_(std::move(host)), dispatcher_(dispatcher), random_(random),
      reporting_period_(reporting_period), callbacks_(callbacks), stats_(stats) {
  RELEASE_ASSERT(reporting_period_.count() > 0, "OrcaOobSession reporting_period must be positive");
  backoff_strategy_ =
      std::make_unique<JitteredExponentialBackOffStrategy>(InitialBackoffMs, MaxBackoffMs, random_);
  reconnect_timer_ = dispatcher_.createTimer([this]() { start(); });
}

OrcaOobSession::~OrcaOobSession() { stop(); }

void OrcaOobSession::start() {
  if (!server_supports_oob_) {
    return;
  }
  // Cancel any pending reconnect up front: a prior transient failure may have
  // armed the timer with a shorter backoff; if the fresh start() here hits
  // encodeHeaders failure, scheduleReconnect's enabled() dedup would otherwise
  // let that old shorter timer win instead of the new (longer) bucket.
  reconnect_timer_->disableTimer();
  // Suppress scheduleReconnect() from re-entrant close callbacks while we tear
  // down any stale client, and reset for the fresh session afterwards.
  suppress_reconnect_ = true;
  clearStreamState();
  if (client_) {
    client_->close(Network::ConnectionCloseType::NoFlush);
    client_.reset();
  }
  suppress_reconnect_ = false;
  createClient();
  startStream();
}

void OrcaOobSession::stop() {
  // Mark the stream closed before closing the client so the re-entrant
  // close()→onEvent(LocalClose)→handleTransientFailure path is a no-op.
  suppress_reconnect_ = true;
  clearStreamState();
  closeClientDeferred();
  reconnect_timer_->disableTimer();
}

Http::CodecClientPtr OrcaOobSession::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  // Codec is H2 since the OOB transport is gRPC-over-H2. Constructing an H2
  // codec on a non-H2 connection is UB, so fail closed instead of attempting
  // construction on a misconfigured cluster.
  if ((data.host_description_->cluster().features() & Upstream::ClusterInfo::Features::HTTP2) ==
      0) {
    ENVOY_LOG(debug, "OrcaOobSession: host {} is not on an HTTP/2-capable cluster",
              data.host_description_->address()->asString());
    return nullptr;
  }
  return std::make_unique<Http::CodecClientProd>(Http::CodecType::HTTP2,
                                                 std::move(data.connection_),
                                                 data.host_description_, dispatcher_, random_,
                                                 /*transport_socket_options=*/nullptr);
}

void OrcaOobSession::createClient() {
  auto conn_data = host_->createConnection(dispatcher_, /*options=*/nullptr,
                                           /*transport_socket_options=*/nullptr);
  if (!conn_data.connection_) {
    ENVOY_LOG(debug, "OrcaOobSession: host {} produced null connection",
              host_->address()->asString());
    stats_.stream_open_failures_.inc();
    scheduleReconnect();
    return;
  }
  client_ = createCodecClient(conn_data);
  if (!client_) {
    stats_.stream_open_failures_.inc();
    server_supports_oob_ = false;
    reconnect_timer_->disableTimer();
    return;
  }
  client_->addConnectionCallbacks(connection_cb_);
  client_->setCodecConnectionCallbacks(http_connection_cb_);
}

void OrcaOobSession::startStream() {
  if (!client_) {
    return;
  }

  // Fresh decoder for this stream with a max-frame cap (DoS safety).
  grpc_decoder_ = Grpc::Decoder();
  grpc_decoder_.setMaxFrameLength(MaxOrcaFrameBytes);

  request_encoder_ = &client_->newStream(*this);
  request_encoder_->getStream().addCallbacks(*this);

  // Prefer hostname when present; otherwise fall back to asString(). The
  // address form ("[::1]:80", "/tmp/foo") is unconventional as :authority but
  // valid for both IP and pipe upstreams.
  const absl::string_view authority =
      host_->hostname().empty() ? host_->address()->asString() : host_->hostname();
  // Deliberately no grpc-timeout: OpenRcaService/StreamCoreMetrics is a
  // long-lived server-streaming RPC. A client deadline would force a server
  // cancel + reconnect every deadline even on the happy path. Dead-peer
  // detection is handled by HTTP/2 keepalive PINGs.
  auto headers = Grpc::Common::prepareHeaders(authority, "xds.service.orca.v3.OpenRcaService",
                                              "StreamCoreMetrics", /*timeout=*/absl::nullopt);
  headers->headers().setReferenceUserAgent(Http::Headers::get().UserAgentValues.EnvoyOrcaOob);
  Router::FilterUtility::setUpstreamScheme(
      headers->headers(),
      /*downstream_ssl=*/false, host_->transportSocketFactory().implementsSecureTransport(),
      /*use_upstream=*/true);

  auto status = request_encoder_->encodeHeaders(headers->headers(), false);
  if (!status.ok()) {
    ENVOY_LOG(debug, "OrcaOobSession: encodeHeaders failed for host {}: {}",
              host_->address()->asString(), status.message());
    // Stream never went active; clearStreamState skips gauge dec.
    clearStreamState();
    if (client_) {
      client_->close(Network::ConnectionCloseType::NoFlush);
      client_.reset();
    }
    stats_.stream_open_failures_.inc();
    scheduleReconnect();
    return;
  }

  xds::service::orca::v3::OrcaLoadReportRequest request;
  *request.mutable_report_interval() =
      Protobuf::util::TimeUtil::MillisecondsToDuration(reporting_period_.count());

  request_encoder_->encodeData(*Grpc::Common::serializeToGrpcFrame(request), true);

  stream_closed_ = false;
  // Cancel any pending reconnect; fresh stream is up.
  reconnect_timer_->disableTimer();
  stats_.streams_started_.inc();
  stats_.active_streams_.inc();
}

void OrcaOobSession::decodeData(Buffer::Instance& data, bool end_stream) {
  // Drop callbacks on a closed stream: the HTTP/2 stream is torn down only
  // when the client is destroyed, so stray frames can arrive in between.
  if (stream_closed_) {
    return;
  }
  std::vector<Grpc::Frame> frames;
  const auto status = grpc_decoder_.decode(data, frames);
  if (!status.ok()) {
    ENVOY_LOG(debug, "OrcaOobSession: gRPC decode error for host {}: {}",
              host_->address()->asString(), status.message());
    handleTransientFailure();
    return;
  }
  for (auto& frame : frames) {
    // Re-check in case stop() was called from within onOrcaOobReport() or
    // onOrcaLoadReportReceived tripped a transient failure on parse error.
    if (stream_closed_) {
      return;
    }
    if (frame.flags_ != Grpc::GRPC_FH_DEFAULT) {
      ENVOY_LOG(debug, "OrcaOobSession: invalid gRPC frame flags {} from host {}", frame.flags_,
                host_->address()->asString());
      stats_.reports_failed_.inc();
      handleTransientFailure();
      return;
    }
    onOrcaLoadReportReceived(std::move(frame.data_));
  }
  if (end_stream && !stream_closed_) {
    softClose();
  }
}

void OrcaOobSession::onOrcaLoadReportReceived(Buffer::InstancePtr&& message) {
  xds::data::orca::v3::OrcaLoadReport report;
  Buffer::ZeroCopyInputStreamImpl stream(std::move(message));
  if (!report.ParseFromZeroCopyStream(&stream)) {
    ENVOY_LOG(debug, "OrcaOobSession: failed to parse OrcaLoadReport from host {}",
              host_->address()->asString());
    stats_.reports_failed_.inc();
    // A parse failure inside a well-formed gRPC frame is a protocol violation.
    // Trip a transient failure so a buggy or malicious server cannot silently
    // burn the reports_failed_ counter forever while the stream stays "healthy".
    handleTransientFailure();
    return;
  }
  // Stream proven live by a real report: reset backoff. 200 headers alone must
  // NOT reset backoff -- a server that hangs up after headers would otherwise
  // tight-loop.
  backoff_strategy_->reset();
  stats_.reports_received_.inc();
  callbacks_.onOrcaOobReport(report);
}

void OrcaOobSession::clearStreamState() {
  if (!stream_closed_) {
    stats_.active_streams_.dec();
  }
  stream_closed_ = true;
  request_encoder_ = nullptr;
}

void OrcaOobSession::handlePermanentFailure() {
  ENVOY_LOG(debug, "OrcaOobSession: permanent failure for host {} (Unimplemented)",
            host_->address()->asString());
  server_supports_oob_ = false;
  clearStreamState();
  reconnect_timer_->disableTimer();
  // Runs inside a decode callback; sync reset would UAF the ActiveRequest.
  closeClientDeferred();
}

bool OrcaOobSession::handleIfGrpcFailure(absl::optional<Grpc::Status::GrpcStatus> grpc_status) {
  if (!grpc_status.has_value() || *grpc_status == Grpc::Status::WellKnownGrpcStatus::Ok) {
    return false;
  }
  if (*grpc_status == Grpc::Status::WellKnownGrpcStatus::Unimplemented) {
    handlePermanentFailure();
    return true;
  }
  // Any remaining non-Ok status is a server-side error (Internal, Unavailable,
  // DeadlineExceeded, ...). Bucket with streams_failed_ so operators watching
  // that counter see it, not with streams_closed_.
  handleTransientFailure();
  return true;
}

void OrcaOobSession::closeClientDeferred() {
  if (!client_) {
    return;
  }
  client_->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_.deferredDelete(std::move(client_));
}

void OrcaOobSession::decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  // See decodeData for the stream_closed_ guard rationale.
  if (stream_closed_) {
    return;
  }
  const auto http_response_status = Http::Utility::getResponseStatus(*headers);
  if (http_response_status != enumToInt(Http::Code::OK)) {
    const auto grpc_status = end_stream ? Grpc::Common::getGrpcStatus(*headers) : absl::nullopt;
    if (handleIfGrpcFailure(grpc_status.has_value()
                                ? grpc_status
                                : absl::optional<Grpc::Status::GrpcStatus>(
                                      Grpc::Utility::httpToGrpcStatus(http_response_status)))) {
      return;
    }
    softClose();
    return;
  }
  if (!Grpc::Common::isGrpcResponseHeaders(*headers, end_stream)) {
    handleTransientFailure();
    return;
  }
  if (!end_stream) {
    return;
  }
  const auto grpc_status = Grpc::Common::getGrpcStatus(*headers);
  if (handleIfGrpcFailure(grpc_status)) {
    return;
  }
  if (!grpc_status.has_value()) {
    handleTransientFailure();
    return;
  }
  softClose();
}

void OrcaOobSession::decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  // See decodeData for the stream_closed_ guard rationale.
  if (stream_closed_) {
    return;
  }
  const auto grpc_status = Grpc::Common::getGrpcStatus(*trailers);
  if (handleIfGrpcFailure(grpc_status)) {
    return;
  }
  if (!grpc_status.has_value()) {
    handleTransientFailure();
    return;
  }
  softClose();
}

void OrcaOobSession::softClose() {
  if (!stream_closed_) {
    stats_.streams_closed_.inc();
  }
  clearStreamState();
  scheduleReconnect();
}

void OrcaOobSession::handleTransientFailure() {
  if (!stream_closed_) {
    stats_.streams_failed_.inc();
  }
  clearStreamState();
  scheduleReconnect();
}

void OrcaOobSession::onResetStream(Http::StreamResetReason, absl::string_view) {
  handleTransientFailure();
}

void OrcaOobSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    handleTransientFailure();
  }
}

void OrcaOobSession::onGoAway(Http::GoAwayErrorCode) {
  // Proactively arm the reconnect timer; codecs may defer close events.
  // Subsequent re-entrant calls are idempotent via stream_closed_/timer dedup.
  handleTransientFailure();
  if (client_) {
    client_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void OrcaOobSession::scheduleReconnect() {
  if (!server_supports_oob_ || suppress_reconnect_ || reconnect_timer_->enabled()) {
    return;
  }
  const uint64_t backoff_ms = backoff_strategy_->nextBackOffMs();
  ENVOY_LOG(debug, "OrcaOobSession: scheduling reconnect to {} in {}ms",
            host_->address()->asString(), backoff_ms);
  reconnect_timer_->enableTimer(std::chrono::milliseconds(backoff_ms));
}

} // namespace Orca
} // namespace Envoy
