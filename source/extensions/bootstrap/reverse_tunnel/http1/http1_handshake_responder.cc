#include "source/extensions/bootstrap/reverse_tunnel/http1/http1_handshake_responder.h"

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

namespace {

class RequestDecoderHandleImpl : public Http::RequestDecoderHandle {
public:
  RequestDecoderHandleImpl(std::weak_ptr<bool> valid, Http::RequestDecoder& decoder)
      : valid_(std::move(valid)), decoder_(decoder) {}
  OptRef<Http::RequestDecoder> get() override {
    if (valid_.expired()) {
      return {};
    }
    return decoder_;
  }

private:
  std::weak_ptr<bool> valid_;
  Http::RequestDecoder& decoder_;
};

} // namespace

Http1HandshakeResponder::Http1HandshakeResponder(ResponderHandshakeCallbacks& callbacks,
                                                 Http::Http1::CodecStats::AtomicPtr& codec_stats,
                                                 const HandshakeRequestSpec& spec)
    : callbacks_(callbacks), codec_stats_(codec_stats), spec_(spec) {}

Network::FilterStatus Http1HandshakeResponder::onData(Buffer::Instance& data) {
  if (!codec_) {
    Http::Http1Settings http1_settings;
    codec_ = std::make_unique<Http::Http1::ServerConnectionImpl>(
        callbacks_.connection(),
        Http::Http1::CodecStats::atomicGet(codec_stats_, callbacks_.statsScope()), *this,
        http1_settings, Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
        envoy::config::core::v3::HttpProtocolOptions::ALLOW, callbacks_.overloadManager());
  }

  const Http::Status status = codec_->dispatch(data);
  if (!status.ok()) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: codec dispatch error: {}", callbacks_.connection(),
                   status.message());
    // A malformed request is terminal, so the handshake deadline no longer applies.
    callbacks_.onTerminalRejection();
    callbacks_.connection().close(Network::ConnectionCloseType::FlushWrite);
  }
  return Network::FilterStatus::StopIteration;
}

Http::RequestDecoder& Http1HandshakeResponder::newStream(Http::ResponseEncoder& response_encoder,
                                                         bool) {
  active_decoder_ = std::make_unique<RequestDecoderImpl>(*this, response_encoder);
  return *active_decoder_;
}

ResponderHandshakePtr
Http1HandshakeResponderConfigurator::createHandshake(ResponderHandshakeCallbacks& callbacks) const {
  return std::make_unique<Http1HandshakeResponder>(callbacks, codec_stats_, spec_);
}

// RequestDecoderImpl
void Http1HandshakeResponder::RequestDecoderImpl::decodeHeaders(
    Http::RequestHeaderMapSharedPtr&& headers, bool end_stream) {
  headers_ = std::move(headers);
  // Reverse tunnel handshakes carry no body. Reject a declared Content-Length up front; a chunked
  // body is caught by decodeData as its bytes arrive. `Content-Length: 0` from older initiators is
  // accepted.
  const absl::string_view content_length = headers_->getContentLengthValue();
  if (!content_length.empty() && content_length != "0") {
    rejectBody();
    return;
  }
  // For an Upgrade request, the HTTP/1 server codec calls decodeHeaders with
  // end_stream=false because it now considers the connection a tunnel awaiting
  // more bytes. The handshake has no body, so process the headers immediately
  // rather than waiting for an end-of-stream that never comes.
  if (end_stream || Http::Utility::isUpgrade(*headers_)) {
    processIfComplete(true);
  }
}

void Http1HandshakeResponder::RequestDecoderImpl::decodeData(Buffer::Instance& data,
                                                             bool end_stream) {
  if (complete_) {
    return;
  }
  // Reverse tunnel handshakes carry no body. Any payload byte is rejected rather than buffered.
  if (data.length() > 0) {
    rejectBody();
    return;
  }
  if (end_stream) {
    processIfComplete(true);
  }
}

void Http1HandshakeResponder::RequestDecoderImpl::rejectBody() {
  if (complete_) {
    return;
  }
  complete_ = true;
  parent_.callbacks_.onFramingRejection(HandshakeFramingRejection::BodyRejected);
  ENVOY_CONN_LOG(debug, "reverse_tunnel: rejecting handshake request with a body",
                 parent_.callbacks_.connection());
  sendLocalReply(Http::Code::BadRequest, "Reverse tunnel handshake must not carry a body", nullptr,
                 std::nullopt, "reverse_tunnel_body_rejected");
  parent_.callbacks_.connection().close(Network::ConnectionCloseType::FlushWrite);
}

void Http1HandshakeResponder::RequestDecoderImpl::decodeTrailers(Http::RequestTrailerMapPtr&&) {
  processIfComplete(true);
}

void Http1HandshakeResponder::RequestDecoderImpl::decodeMetadata(Http::MetadataMapPtr&&) {}

void Http1HandshakeResponder::RequestDecoderImpl::sendLocalReply(
    Http::Code code, absl::string_view body,
    const std::function<void(Http::ResponseHeaderMap& headers)>& modify_headers,
    const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
  // A local reply is always a terminal rejection, so the handshake deadline no longer applies.
  parent_.callbacks_.onTerminalRejection();
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(static_cast<uint64_t>(code));
  headers->setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
  if (modify_headers) {
    modify_headers(*headers);
  }
  const bool end_stream = body.empty();
  encoder_.encodeHeaders(*headers, end_stream);
  if (!end_stream) {
    Buffer::OwnedImpl buf(body);
    encoder_.encodeData(buf, true);
  }
}

StreamInfo::StreamInfo& Http1HandshakeResponder::RequestDecoderImpl::streamInfo() {
  return stream_info_;
}

AccessLog::InstanceSharedPtrVector
Http1HandshakeResponder::RequestDecoderImpl::accessLogHandlers() {
  return {};
}

Http::RequestDecoderHandlePtr
Http1HandshakeResponder::RequestDecoderImpl::getRequestDecoderHandle() {
  return std::make_unique<RequestDecoderHandleImpl>(still_alive_, *this);
}

void Http1HandshakeResponder::RequestDecoderImpl::processIfComplete(bool end_stream) {
  if (!end_stream || complete_) {
    return;
  }
  complete_ = true;

  ResponderHandshakeCallbacks& callbacks = parent_.callbacks_;

  // Validate method/path.
  const absl::string_view method = headers_->getMethodValue();
  const absl::string_view path = headers_->getPathValue();
  ENVOY_LOG(trace,
            "ReverseTunnelFilter::RequestDecoderImpl::processIfComplete: method: {}, path: {}",
            method, path);
  if (!absl::EqualsIgnoreCase(method, parent_.spec_.method) || path != parent_.spec_.path) {
    sendLocalReply(Http::Code::NotFound, "Not a reverse tunnel request", nullptr, std::nullopt,
                   "reverse_tunnel_not_found");
    // Close the connection after sending the response.
    callbacks.connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  // When upgrade negotiation is enabled, require the request to advertise the
  // `reverse-tunnel` upgrade. The HTTP/1 server codec already validates the
  // `Connection: Upgrade` paired token, so we only re-check the `Upgrade` value here.
  if (parent_.spec_.use_http_upgrade) {
    const auto upgrade = headers_->getUpgradeValue();
    if (!absl::EqualsIgnoreCase(upgrade, Bootstrap::ReverseConnection::ReverseConnectionUtility::
                                             REVERSE_TUNNEL_UPGRADE_PROTOCOL)) {
      callbacks.onFramingRejection(HandshakeFramingRejection::ParseError);
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: upgrade negotiation enabled but Upgrade header missing or "
                     "unexpected (got '{}')",
                     callbacks.connection(), upgrade);
      sendLocalReply(
          Http::Code::UpgradeRequired, "Upgrade: reverse-tunnel required",
          [](Http::ResponseHeaderMap& h) {
            h.setReferenceKey(Http::Headers::get().Upgrade,
                              Bootstrap::ReverseConnection::ReverseConnectionUtility::
                                  REVERSE_TUNNEL_UPGRADE_PROTOCOL);
          },
          std::nullopt, "reverse_tunnel_upgrade_required");
      callbacks.connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
  }

  // Extract node/cluster/tenant identifiers from HTTP headers.
  const auto node_vals = headers_->get(Bootstrap::ReverseConnection::reverseTunnelNodeIdHeader());
  const auto cluster_vals =
      headers_->get(Bootstrap::ReverseConnection::reverseTunnelClusterIdHeader());
  const auto tenant_vals =
      headers_->get(Bootstrap::ReverseConnection::reverseTunnelTenantIdHeader());

  if (node_vals.empty() || cluster_vals.empty() || tenant_vals.empty()) {
    callbacks.onFramingRejection(HandshakeFramingRejection::ParseError);
    ENVOY_CONN_LOG(debug, "reverse_tunnel: missing required headers (node/cluster/tenant)",
                   callbacks.connection());
    sendLocalReply(Http::Code::BadRequest, "Missing required reverse tunnel headers", nullptr,
                   std::nullopt, "reverse_tunnel_missing_headers");
    // Close the connection after sending the response.
    callbacks.connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  const absl::string_view node_id = node_vals[0]->value().getStringView();
  const absl::string_view cluster_id = cluster_vals[0]->value().getStringView();
  const absl::string_view tenant_id = tenant_vals[0]->value().getStringView();

  // Initiator metadata is optional, so older initiators that do not advertise it still connect.
  const auto first_value = [this](const Http::LowerCaseString& key) -> absl::string_view {
    const auto vals = headers_->get(key);
    return vals.empty() ? absl::string_view{} : vals[0]->value().getStringView();
  };
  const auto initiation_time_vals =
      headers_->get(Bootstrap::ReverseConnection::reverseTunnelInitiationTimeHeader());
  const TunnelRequest request{
      node_id,
      cluster_id,
      tenant_id,
      *headers_,
      initiation_time_vals.empty()
          ? std::nullopt
          : std::make_optional(initiation_time_vals[0]->value().getStringView()),
      first_value(Bootstrap::ReverseConnection::reverseTunnelWorkerIdHeader()),
      first_value(Bootstrap::ReverseConnection::reverseTunnelConnectionIdHeader())};

  // Policy checks and phase one of acceptance. On accept the filter has duplicated the socket and
  // disabled reads; on reject the reply below is terminal and the connection is closed.
  const TunnelDecision decision = callbacks.onTunnelRequest(request);
  if (!decision.accepted) {
    sendLocalReply(decision.code, decision.body, nullptr, std::nullopt, decision.details);
    callbacks.connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  // Send the acceptance response. In upgrade mode this is `101 Switching Protocols`.
  auto resp_headers = Http::ResponseHeaderMapImpl::create();
  if (parent_.spec_.use_http_upgrade) {
    resp_headers->setStatus(101);
    resp_headers->setReferenceKey(Http::Headers::get().Connection,
                                  Http::Headers::get().ConnectionValues.Upgrade);
    resp_headers->setReferenceKey(
        Http::Headers::get().Upgrade,
        Bootstrap::ReverseConnection::ReverseConnectionUtility::REVERSE_TUNNEL_UPGRADE_PROTOCOL);
  } else {
    resp_headers->setStatus(200);
  }
  encoder_.encodeHeaders(*resp_headers, true);

  // Phase two runs once the acceptance response has reached the wire, so the response is ordered
  // ahead of any first byte a consumer writes into the tunnel. The count must be the exact size of
  // the encoded response, which is all this stream sends.
  const uint64_t response_wire_bytes = encoder_.getStream().bytesMeter()->wireBytesSent();
  callbacks.onAcceptanceResponseEncoded(response_wire_bytes);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
