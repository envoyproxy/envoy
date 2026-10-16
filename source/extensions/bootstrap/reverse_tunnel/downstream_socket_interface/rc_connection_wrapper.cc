#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/rc_connection_wrapper.h"

#include <algorithm>
#include <optional>

#include "envoy/network/address.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// RCConnectionWrapper constructor implementation
RCConnectionWrapper::RCConnectionWrapper(ReverseConnectionIOHandle& parent,
                                         Network::ClientConnectionPtr connection,
                                         Upstream::HostDescriptionConstSharedPtr host,
                                         const std::string& cluster_name)
    : parent_(parent), connection_(std::move(connection)), host_(std::move(host)),
      cluster_name_(cluster_name) {
  ENVOY_LOG(debug, "RCConnectionWrapper: Using HTTP handshake for reverse connections");
}

// RCConnectionWrapper destructor implementation.
RCConnectionWrapper::~RCConnectionWrapper() {
  ENVOY_LOG(debug, "RCConnectionWrapper destructor called");
  if (!shutdown_called_) {
    this->shutdown();
  }
}

void RCConnectionWrapper::onEvent(Network::ConnectionEvent event) {
  // Any close before the handshake completes is terminal for this attempt. A successful handshake
  // removes these callbacks, so a close seen here always means the peer or local stack dropped the
  // connection first.
  if (event != Network::ConnectionEvent::RemoteClose &&
      event != Network::ConnectionEvent::LocalClose) {
    return;
  }
  if (!connection_) {
    ENVOY_LOG(debug, "RCConnectionWrapper: connection is null, skipping event handling");
    return;
  }

  // Store connection info before it gets invalidated.
  const std::string connection_key =
      connection_->connectionInfoProvider().localAddress()->asString();
  const uint64_t connection_id = connection_->id();

  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, connection {} closed before handshake",
            connection_id, connection_key);

  // Do not call shutdown() here, as that may trigger cleanup during event processing. Notify the
  // parent instead, which treats the premature close as a terminal handshake failure.
  parent_.onConnectionDone("Connection closed", this, true);
}

// SimpleConnReadFilter::onData implementation.
Network::FilterStatus SimpleConnReadFilter::onData(Buffer::Instance& buffer, bool end_stream) {
  if (parent_ == nullptr) {
    return Network::FilterStatus::StopIteration;
  }

  // Cast parent_ back to RCConnectionWrapper.
  RCConnectionWrapper* wrapper = static_cast<RCConnectionWrapper*>(parent_);

  wrapper->dispatchHttp1(buffer);
  UNREFERENCED_PARAMETER(end_stream);
  return Network::FilterStatus::StopIteration;
}

absl::Status RCConnectionWrapper::connect(const std::string& src_tenant_id,
                                          const std::string& src_cluster_id,
                                          const std::string& src_node_id,
                                          std::optional<int64_t> initiation_time_ms) {
  // Register connection callbacks.
  ENVOY_LOG(debug, "RCConnectionWrapper: connection: {}, adding connection callbacks",
            connection_->id());
  connection_->addConnectionCallbacks(*this);
  connection_->connect();

  // Use HTTP handshake.
  ENVOY_LOG(debug,
            "RCConnectionWrapper: connection: {}, sending reverse connection creation "
            "request through HTTP",
            connection_->id());

  // Create HTTP/1 codec to parse the response.
  Http::Http1Settings http1_settings = host_->cluster().httpProtocolOptions().http1Settings();
  http1_client_codec_ = std::make_unique<Http::Http1::ClientConnectionImpl>(
      *connection_, host_->cluster().http1CodecStats(), *this, http1_settings,
      host_->cluster().maxResponseHeadersKb(), host_->cluster().maxResponseHeadersCount());
  http1_parse_connection_ = http1_client_codec_.get();

  // Add a tiny read filter to feed bytes into the codec for response parsing.
  connection_->addReadFilter(read_filter_);

  // Build HTTP handshake headers with identifiers.
  absl::string_view tenant_id = src_tenant_id;
  absl::string_view cluster_id = src_cluster_id;
  absl::string_view node_id = src_node_id;
  // EnvoyInternal remote clusters are rejected before the dial, so the remote address is always a
  // real network address.
  const std::string& host_value = connection_->connectionInfoProvider().remoteAddress()->asString();
  const Http::LowerCaseString& node_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelNodeIdHeader();
  const Http::LowerCaseString& cluster_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelClusterIdHeader();
  const Http::LowerCaseString& tenant_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelTenantIdHeader();
  const Http::LowerCaseString& upstream_cluster_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelUpstreamClusterNameHeader();

  auto headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
      {{Http::Headers::get().Method, Http::Headers::get().MethodValues.Get},
       {Http::Headers::get().Path, parent_.requestPath()},
       {Http::Headers::get().Host, host_value}});
  if (parent_.useHttpUpgrade()) {
    // Negotiate the handshake as an HTTP/1.1 Upgrade so HCM `upgrade_configs` can splice
    // the connection raw after `101 Switching Protocols`. Both ends must agree.
    headers->setReferenceKey(Http::Headers::get().Connection,
                             Http::Headers::get().ConnectionValues.Upgrade);
    headers->setReferenceKey(Http::Headers::get().Upgrade,
                             ::Envoy::Extensions::Bootstrap::ReverseConnection::
                                 ReverseConnectionUtility::REVERSE_TUNNEL_UPGRADE_PROTOCOL);
  }
  headers->addCopy(node_hdr, std::string(node_id));
  headers->addCopy(cluster_hdr, std::string(cluster_id));
  headers->addCopy(tenant_hdr, std::string(tenant_id));
  headers->addCopy(upstream_cluster_hdr, cluster_name_);

  const Http::LowerCaseString& initiation_time_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelInitiationTimeHeader();
  // Prefer the episode initiation time supplied by the caller so that handshake retries during
  // initial establishment carry the original intent time; fall back to now for direct callers.
  const int64_t initiation_ms =
      initiation_time_ms.has_value()
          ? *initiation_time_ms
          : std::chrono::duration_cast<std::chrono::milliseconds>(
                connection_->dispatcher().timeSource().systemTime().time_since_epoch())
                .count();
  headers->addCopy(initiation_time_hdr, absl::StrCat(initiation_ms));

  // Advertise which initiator worker and connection opened this tunnel so the two ends can be
  // correlated and tunnels from different workers/connections told apart.
  const Http::LowerCaseString& worker_id_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelWorkerIdHeader();
  const Http::LowerCaseString& connection_id_hdr =
      ::Envoy::Extensions::Bootstrap::ReverseConnection::reverseTunnelConnectionIdHeader();
  headers->addCopy(worker_id_hdr, connection_->dispatcher().name());
  headers->addCopy(connection_id_hdr, absl::StrCat(connection_->id()));

  using HeaderValueOption = envoy::config::core::v3::HeaderValueOption;
  const auto apply_header = [&headers](const Http::LowerCaseString& key, absl::string_view value,
                                       HeaderValueOption::HeaderAppendAction action) {
    switch (action) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
      headers->addCopy(key, value);
      break;
    case HeaderValueOption::ADD_IF_ABSENT:
      if (headers->get(key).empty()) {
        headers->addCopy(key, value);
      }
      break;
    case HeaderValueOption::OVERWRITE_IF_EXISTS:
      if (!headers->get(key).empty()) {
        headers->setCopy(key, value);
      }
      break;
    case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
      headers->setCopy(key, value);
      break;
    }
  };

  // Read the handshake formatters from the live extension, not from the io_handle's snapshot. The
  // listen socket can be created before onServerInitialized() builds the formatters, so the
  // snapshot taken at socket creation (parent_.handshakeHeaders()) may be null. That snapshot is
  // reused for every re-dial, so trusting it would send the raw additional_headers() value instead
  // of the formatted one. The extension always has the built formatters by the time we send a
  // handshake; fall back to the snapshot only when the extension has none (it's just a copy of it
  // anyway).
  ReverseTunnelInitiatorExtension* extension = getDownstreamExtension();
  const HandshakeHeadersConstSharedPtr handshake_headers =
      (extension != nullptr && extension->handshakeHeaders() != nullptr)
          ? extension->handshakeHeaders()
          : parent_.handshakeHeaders();
  if (handshake_headers != nullptr) {
    StreamInfo::StreamInfoImpl stream_info(connection_->dispatcher().timeSource(), nullptr,
                                           StreamInfo::FilterState::LifeSpan::Connection);
    for (const auto& hdr : *handshake_headers) {
      apply_header(hdr.key, hdr.value_formatter->format({}, stream_info), hdr.append_action);
    }
  } else {
    for (const auto& h : parent_.additionalHeaders()) {
      apply_header(Http::LowerCaseString(h.header().key()), h.header().value(), h.append_action());
    }
  }
  headers->setContentLength(0);

  // Encode via HTTP/1 codec.
  Http::RequestEncoder& request_encoder = http1_client_codec_->newStream(*this);
  const Http::Status encode_status = request_encoder.encodeHeaders(*headers, true);
  if (!encode_status.ok()) {
    ENVOY_LOG(error, "RCConnectionWrapper: encodeHeaders failed: {}", encode_status.message());
    onHandshakeFailure(HandshakeFailureReason::encodeError());
    return absl::InternalError(absl::StrCat("handshake encode failed: ", encode_status.message()));
  }

  // Arm the handshake response deadline so a peer that completes TCP and TLS but never answers
  // cannot hold the attempt open forever.
  handshake_timer_ = connection_->dispatcher().createTimer([this]() { onHandshakeTimeout(); });
  handshake_timer_->enableTimer(parent_.handshakeTimeout());

  return absl::OkStatus();
}

void RCConnectionWrapper::onHandshakeTimeout() {
  ENVOY_LOG(debug, "RCConnectionWrapper: connection {} handshake timed out",
            connection_ ? connection_->id() : 0);
  onHandshakeFailure(HandshakeFailureReason::timeout());
}

void RCConnectionWrapper::disarmHandshakeTimer() {
  if (handshake_timer_ != nullptr) {
    handshake_timer_->disableTimer();
  }
}

void RCConnectionWrapper::decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool) {
  const uint64_t status = Http::Utility::getResponseStatus(*headers);
  const uint64_t expected = parent_.useHttpUpgrade() ? 101 : 200;
  if (status == expected) {
    ENVOY_LOG(debug, "Received HTTP {} response", status);
    onHandshakeSuccess();
    return;
  }
  ENVOY_LOG(error, "Received unexpected HTTP response: {} (expected {})", status, expected);
  // A 429 may carry a Retry-After cool-off hint; forward it so the parent can honor it as the
  // per-host backoff before the next attempt.
  std::optional<std::chrono::milliseconds> retry_after;
  if (status == 429) {
    retry_after = parseRetryAfter(*headers);
  }
  onHandshakeFailure(HandshakeFailureReason::httpStatusError(absl::StrCat(status)), retry_after);
}

std::optional<std::chrono::milliseconds>
RCConnectionWrapper::parseRetryAfter(const Http::ResponseHeaderMap& headers) {
  // ``Retry-After`` is not a registered inline header, so look it up by name. The key is a
  // function-local static to avoid reconstructing the LowerCaseString on every handshake response.
  static const Http::LowerCaseString retry_after_header{"retry-after"};
  const auto result = headers.get(retry_after_header);
  if (result.empty()) {
    return std::nullopt;
  }
  const absl::string_view value = result[0]->value().getStringView();
  // RFC 7231 allows delta-seconds or an HTTP-date. Rate limiters emit delta-seconds; honor that
  // form and ignore the date form (the caller falls back to its computed backoff). A zero (or
  // unparseable) value is treated as absent so it cannot short-circuit the backoff.
  uint64_t seconds = 0;
  if (!absl::SimpleAtoi(value, &seconds) || seconds == 0) {
    return std::nullopt;
  }
  // Clamp purely to avoid overflow when widening seconds to milliseconds; this is NOT the backoff
  // policy cap. The effective ceiling (the configured ``max_reconnect_backoff``) is applied by the
  // caller in trackConnectionFailure(), so this bound is intentionally far above any sane value.
  constexpr uint64_t kMaxRetryAfterSeconds = 3600; // 1 hour.
  seconds = std::min(seconds, kMaxRetryAfterSeconds);
  return std::chrono::seconds(static_cast<int64_t>(seconds));
}

void RCConnectionWrapper::dispatchHttp1(Buffer::Instance& buffer) {
  if (http1_parse_connection_ == nullptr) {
    return;
  }
  const Http::Status status = http1_parse_connection_->dispatch(buffer);

  // On success the handoff runs here so bytes the responder coalesced with the response are
  // captured from the buffer before the tunnel is queued. The codec reports those trailing bytes as
  // an extraneous-data error, which is expected and superseded by the successful handshake.
  if (pending_handoff_) {
    captureHandshakeResidual(buffer);
    completeHandshakeHandoff();
    return;
  }

  if (!status.ok()) {
    ENVOY_LOG(debug, "RCConnectionWrapper: HTTP/1 codec dispatch error: {}", status.message());
    // A malformed handshake response is unusable, so fail the attempt now rather than wait for the
    // deadline.
    onHandshakeFailure(HandshakeFailureReason::connectionClose(status.message()));
  }
}

void RCConnectionWrapper::decodeData(Buffer::Instance& data, bool) {
  // In HTTP/1 upgrade mode the responder's post-101 bytes arrive here instead of staying in the
  // dispatch buffer, so carry them with the tunnel to replay before the socket is read.
  if (pending_handoff_) {
    captureHandshakeResidual(data);
  }
}

void RCConnectionWrapper::captureHandshakeResidual(Buffer::Instance& buffer) {
  if (buffer.length() == 0) {
    return;
  }
  if (handshake_residual_ == nullptr) {
    handshake_residual_ = std::make_unique<Buffer::OwnedImpl>();
  }
  handshake_residual_->move(buffer);
}

ReverseTunnelInitiatorExtension* RCConnectionWrapper::getDownstreamExtension() const {
  return parent_.getDownstreamExtension();
}

Network::ClientConnectionPtr RCConnectionWrapper::releaseConnection() {
  if (!connection_) {
    return nullptr;
  }

  connection_->removeConnectionCallbacks(*this);
  connection_->removeReadFilter(read_filter_);
  return std::move(connection_);
}

void RCConnectionWrapper::onHandshakeSuccess() {
  if (handshake_completed_) {
    return;
  }
  handshake_completed_ = true;
  disarmHandshakeTimer();

  ENVOY_LOG(debug, "handshake succeeded");

  // Track handshake success stats.
  auto* extension = getDownstreamExtension();
  if (extension) {
    extension->incrementHandshakeStats(cluster_name_, true, "");
  }

  // Defer the handoff to completeHandshakeHandoff(), run by dispatchHttp1() once dispatch returns,
  // so bytes the responder coalesced with the response are captured and carried with the tunnel.
  pending_handoff_ = true;
}

void RCConnectionWrapper::completeHandshakeHandoff() {
  pending_handoff_ = false;
  parent_.onConnectionDone("reverse connection accepted", this, false);
}

void RCConnectionWrapper::onHandshakeFailure(const HandshakeFailureReason& reason,
                                             std::optional<std::chrono::milliseconds> retry_after) {
  if (handshake_completed_) {
    return;
  }
  handshake_completed_ = true;
  disarmHandshakeTimer();

  const std::string error_message = reason.getDetailedName();
  const std::string stats_failure_reason = reason.getNameForStats();

  ENVOY_LOG(trace, "handshake failed: {}", error_message);

  // Track handshake failure stats.
  auto* extension = getDownstreamExtension();
  if (extension) {
    extension->incrementHandshakeStats(cluster_name_, false, stats_failure_reason);
  }

  parent_.onConnectionDone(error_message, this, false, retry_after);
}

void RCConnectionWrapper::shutdown() {
  if (shutdown_called_) {
    ENVOY_LOG(debug, "RCConnectionWrapper: Shutdown already called, skipping");
    return;
  }
  shutdown_called_ = true;

  // Stop the handshake deadline and reads before tearing down the connection.
  handshake_timer_.reset();
  read_filter_->clearParent();
  http1_parse_connection_ = nullptr;
  http1_client_codec_.reset();

  if (!connection_) {
    ENVOY_LOG(debug, "RCConnectionWrapper: Connection already null, nothing to shutdown");
    return;
  }

  // Get connection info for logging.
  uint64_t connection_id = connection_->id();
  Network::Connection::State state = connection_->state();
  ENVOY_LOG(debug, "RCConnectionWrapper: Shutting down connection ID: {}, state: {}", connection_id,
            static_cast<int>(state));

  connection_->removeConnectionCallbacks(*this);
  connection_->removeReadFilter(read_filter_);

  // Close before deferred-delete so ConnectionImpl is not destroyed with an open socket.
  if (connection_->state() == Network::Connection::State::Open) {
    if (connection_->getSocket()) {
      connection_->getSocket()->ioHandle().resetFileEvents();
    }
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
  connection_->dispatcher().deferredDelete(std::move(connection_));
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
