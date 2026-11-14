#include "source/extensions/bootstrap/reverse_tunnel/http1/http1_handshake_initiator.h"

#include <algorithm>

#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.validate.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// SimpleConnReadFilter::onData implementation.
Network::FilterStatus SimpleConnReadFilter::onData(Buffer::Instance& buffer, bool end_stream) {
  if (parent_ == nullptr) {
    return Network::FilterStatus::StopIteration;
  }

  auto* handshake = static_cast<Http1HandshakeInitiator*>(parent_);

  handshake->onData(buffer);
  UNREFERENCED_PARAMETER(end_stream);
  return Network::FilterStatus::StopIteration;
}

Http1HandshakeInitiator::Http1HandshakeInitiator(InitiatorHandshakeCallbacks& callbacks)
    : callbacks_(callbacks) {}

absl::Status Http1HandshakeInitiator::start(std::optional<int64_t> initiation_time_ms) {
  Network::ClientConnection& connection = callbacks_.connection();

  // Create HTTP/1 codec to parse the response.
  Http::Http1Settings http1_settings =
      callbacks_.host()->cluster().httpProtocolOptions().http1Settings();
  http1_client_codec_ = std::make_unique<Http::Http1::ClientConnectionImpl>(
      connection, callbacks_.host()->cluster().http1CodecStats(), *this, http1_settings,
      callbacks_.host()->cluster().maxResponseHeadersKb(),
      callbacks_.host()->cluster().maxResponseHeadersCount());
  http1_parse_connection_ = http1_client_codec_.get();

  // Add a tiny read filter to feed bytes into the codec for response parsing.
  connection.addReadFilter(read_filter_);

  // Build HTTP handshake headers with identifiers.
  absl::string_view tenant_id = callbacks_.srcTenantId();
  absl::string_view cluster_id = callbacks_.srcClusterId();
  absl::string_view node_id = callbacks_.srcNodeId();
  // EnvoyInternal remote clusters are rejected before the dial, so the remote address is always a
  // real network address.
  const std::string& host_value = connection.connectionInfoProvider().remoteAddress()->asString();
  const Http::LowerCaseString& node_hdr = reverseTunnelNodeIdHeader();
  const Http::LowerCaseString& cluster_hdr = reverseTunnelClusterIdHeader();
  const Http::LowerCaseString& tenant_hdr = reverseTunnelTenantIdHeader();
  const Http::LowerCaseString& upstream_cluster_hdr = reverseTunnelUpstreamClusterNameHeader();

  auto headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
      {{Http::Headers::get().Method, Http::Headers::get().MethodValues.Get},
       {Http::Headers::get().Path, callbacks_.requestPath()},
       {Http::Headers::get().Host, host_value}});
  if (callbacks_.useHttpUpgrade()) {
    // Negotiate the handshake as an HTTP/1.1 Upgrade so HCM `upgrade_configs` can splice
    // the connection raw after `101 Switching Protocols`. Both ends must agree.
    headers->setReferenceKey(Http::Headers::get().Connection,
                             Http::Headers::get().ConnectionValues.Upgrade);
    headers->setReferenceKey(Http::Headers::get().Upgrade,
                             ReverseConnectionUtility::REVERSE_TUNNEL_UPGRADE_PROTOCOL);
  }
  headers->addCopy(node_hdr, node_id);
  headers->addCopy(cluster_hdr, cluster_id);
  headers->addCopy(tenant_hdr, tenant_id);
  headers->addCopy(upstream_cluster_hdr, callbacks_.clusterName());

  const Http::LowerCaseString& initiation_time_hdr = reverseTunnelInitiationTimeHeader();
  // Prefer the episode initiation time supplied by the caller so that handshake retries during
  // initial establishment carry the original intent time; fall back to now for direct callers.
  const int64_t initiation_ms =
      initiation_time_ms.has_value()
          ? *initiation_time_ms
          : std::chrono::duration_cast<std::chrono::milliseconds>(
                connection.dispatcher().timeSource().systemTime().time_since_epoch())
                .count();
  headers->addCopy(initiation_time_hdr, absl::StrCat(initiation_ms));

  // Advertise which initiator worker and connection opened this tunnel so the two ends can be
  // correlated and tunnels from different workers/connections told apart.
  const Http::LowerCaseString& worker_id_hdr = reverseTunnelWorkerIdHeader();
  const Http::LowerCaseString& connection_id_hdr = reverseTunnelConnectionIdHeader();
  headers->addCopy(worker_id_hdr, connection.dispatcher().name());
  headers->addCopy(connection_id_hdr, absl::StrCat(connection.id()));

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

  const HandshakeHeadersConstSharedPtr handshake_headers = callbacks_.effectiveHandshakeHeaders();
  if (handshake_headers != nullptr) {
    StreamInfo::StreamInfoImpl stream_info(connection.dispatcher().timeSource(), nullptr,
                                           StreamInfo::FilterState::LifeSpan::Connection);
    for (const auto& hdr : *handshake_headers) {
      apply_header(hdr.key, hdr.value_formatter->format({}, stream_info), hdr.append_action);
    }
  } else {
    for (const auto& h : callbacks_.additionalHeaders()) {
      apply_header(Http::LowerCaseString(h.header().key()), h.header().value(), h.append_action());
    }
  }
  headers->setContentLength(0);

  // Encode via HTTP/1 codec.
  Http::RequestEncoder& request_encoder = http1_client_codec_->newStream(*this);
  const Http::Status encode_status = request_encoder.encodeHeaders(*headers, true);
  if (!encode_status.ok()) {
    ENVOY_LOG(error, "RCConnectionWrapper: encodeHeaders failed: {}", encode_status.message());
    terminal_reported_ = true;
    callbacks_.onHandshakeDone(HandshakeOutcome::failure(HandshakeFailureReason::encodeError()),
                               std::nullopt);
    return absl::InternalError(absl::StrCat("handshake encode failed: ", encode_status.message()));
  }

  return absl::OkStatus();
}

void Http1HandshakeInitiator::decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool) {
  // Classification happens exactly once: after a success, ``pending_handoff_`` holds until the
  // post-dispatch handoff reports the terminal outcome; after a failure, ``terminal_reported_``
  // holds. A second header block within one dispatch is therefore dropped.
  if (terminal_reported_ || pending_handoff_) {
    return;
  }
  const uint64_t status = Http::Utility::getResponseStatus(*headers);
  const uint64_t expected = callbacks_.useHttpUpgrade() ? 101 : 200;
  if (status == expected) {
    ENVOY_LOG(debug, "Received HTTP {} response", status);
    callbacks_.onHandshakeSucceeded();
    pending_handoff_ = true;
    return;
  }
  ENVOY_LOG(error, "Received unexpected HTTP response: {} (expected {})", status, expected);
  // The failure is terminal: drop any further codec events so onHandshakeDone fires exactly once.
  terminal_reported_ = true;
  // A 429 may carry a Retry-After cool-off hint; forward it so the parent can honor it as the
  // per-host backoff before the next attempt.
  std::optional<std::chrono::milliseconds> retry_after;
  if (status == 429) {
    retry_after = parseRetryAfter(*headers);
  }
  callbacks_.onHandshakeDone(
      HandshakeOutcome::failure(HandshakeFailureReason::httpStatusError(absl::StrCat(status))),
      retry_after);
}

std::optional<std::chrono::milliseconds>
Http1HandshakeInitiator::parseRetryAfter(const Http::ResponseHeaderMap& headers) {
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

void Http1HandshakeInitiator::onData(Buffer::Instance& buffer) { dispatchHttp1(buffer); }

void Http1HandshakeInitiator::dispatchHttp1(Buffer::Instance& buffer) {
  if (http1_parse_connection_ == nullptr || terminal_reported_) {
    return;
  }
  const Http::Status status = http1_parse_connection_->dispatch(buffer);

  // On success the handoff runs here so bytes the responder coalesced with the response are
  // captured from the buffer before the tunnel is queued. The codec reports those trailing bytes as
  // an extraneous-data error, which is expected and superseded by the successful handshake.
  if (pending_handoff_) {
    pending_handoff_ = false;
    captureHandshakeResidual(buffer);
    terminal_reported_ = true;
    callbacks_.onHandshakeDone(HandshakeOutcome::success(), std::nullopt);
    return;
  }

  if (!status.ok() && !terminal_reported_) {
    ENVOY_LOG(debug, "RCConnectionWrapper: HTTP/1 codec dispatch error: {}", status.message());
    // A malformed handshake response is unusable, so fail the attempt now rather than wait for the
    // deadline.
    terminal_reported_ = true;
    callbacks_.onHandshakeDone(
        HandshakeOutcome::failure(HandshakeFailureReason::connectionClose(status.message())),
        std::nullopt);
  }
}

void Http1HandshakeInitiator::decodeData(Buffer::Instance& data, bool) {
  // In HTTP/1 upgrade mode the responder's post-101 bytes arrive here instead of staying in the
  // dispatch buffer, so carry them with the tunnel to replay before the socket is read.
  if (pending_handoff_) {
    captureHandshakeResidual(data);
  }
}

void Http1HandshakeInitiator::captureHandshakeResidual(Buffer::Instance& buffer) {
  if (buffer.length() == 0) {
    return;
  }
  if (handshake_residual_ == nullptr) {
    handshake_residual_ = std::make_unique<Buffer::OwnedImpl>();
  }
  handshake_residual_->move(buffer);
}

void Http1HandshakeInitiator::cancel() {
  read_filter_->clearParent();
  http1_parse_connection_ = nullptr;
  http1_client_codec_.reset();
  callbacks_.connection().removeReadFilter(read_filter_);
}

Buffer::InstancePtr Http1HandshakeInitiator::takeResidual() {
  return std::move(handshake_residual_);
}

InitiatorHandshakeConfiguratorSharedPtr
Http1InitiatorHandshakeConfiguratorFactory::createConfigurator(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& http_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
          DownstreamReverseConnectionSocketInterface::HttpHandshakeConfig&>(
      config, context.messageValidationVisitor());
  if (!http_config.request_path().empty() || http_config.additional_headers_size() > 0 ||
      http_config.use_http_upgrade()) {
    throw EnvoyException(
        "reverse_tunnel: custom_handshake typed_config for "
        "'envoy.reverse_tunnel.initiator_handshake.http1' does not support request_path, "
        "additional_headers, or use_http_upgrade; the HTTP/1.1 handshake selected through "
        "custom_handshake uses the default request framing");
  }
  return std::make_shared<Http1HandshakeConfigurator>(
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(http_config, handshake_timeout, 15000)));
}

REGISTER_FACTORY(Http1InitiatorHandshakeConfiguratorFactory, InitiatorHandshakeConfiguratorFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
