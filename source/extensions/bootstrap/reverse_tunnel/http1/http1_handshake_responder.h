#pragma once

#include <memory>

#include "envoy/http/codec.h"

#include "source/common/common/logger.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/handshake_responder.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * HTTP/1.1 responder handshake. Parses the handshake request with an HTTP/1 server codec, enforces
 * the request framing (method, path, upgrade token, no body), runs the policy gate on the filter,
 * and emits the acceptance or rejection response.
 */
class Http1HandshakeResponder : public ResponderHandshake,
                                public Http::ServerConnectionCallbacks,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  Http1HandshakeResponder(ResponderHandshakeCallbacks& callbacks,
                          Http::Http1::CodecStats::AtomicPtr& codec_stats);

  // ResponderHandshake
  Network::FilterStatus onData(Buffer::Instance& data) override;

  // Http::ServerConnectionCallbacks
  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder,
                                  bool is_internally_created) override;
  void onGoAway(Http::GoAwayErrorCode) override {}

private:
  // Per-request decoder to buffer the body and respond via the encoder.
  class RequestDecoderImpl : public Http::RequestDecoder {
  public:
    RequestDecoderImpl(Http1HandshakeResponder& parent, Http::ResponseEncoder& encoder)
        : parent_(parent), encoder_(encoder),
          stream_info_(parent_.callbacks_.connection().streamInfo().timeSource(), nullptr,
                       StreamInfo::FilterState::LifeSpan::Connection) {}

    void decodeHeaders(Http::RequestHeaderMapSharedPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::RequestTrailerMapPtr&&) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override;
    void sendLocalReply(Http::Code code, absl::string_view body,
                        const std::function<void(Http::ResponseHeaderMap& headers)>&,
                        const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) override;
    StreamInfo::StreamInfo& streamInfo() override;
    AccessLog::InstanceSharedPtrVector accessLogHandlers() override;
    Http::RequestDecoderHandlePtr getRequestDecoderHandle() override;

  private:
    // Runs the handshake once the request is complete.
    void processIfComplete(bool end_stream);

    // Rejects a handshake request that carries a body, which reverse tunnel handshakes never do.
    void rejectBody();

    Http1HandshakeResponder& parent_;
    Http::ResponseEncoder& encoder_;
    Http::RequestHeaderMapSharedPtr headers_;
    bool complete_{false};
    StreamInfo::StreamInfoImpl stream_info_;

    // Liveness token for the weak request decoder handle. Reset when this decoder is destroyed so
    // a held handle reports the decoder as gone rather than dangling.
    const std::shared_ptr<bool> still_alive_{std::make_shared<bool>(true)};
  };

  ResponderHandshakeCallbacks& callbacks_;
  Http::Http1::CodecStats::AtomicPtr& codec_stats_;
  Http::ServerConnectionPtr codec_;
  std::unique_ptr<RequestDecoderImpl> active_decoder_;
};

/**
 * Factory for the HTTP/1.1 responder handshake. One instance per filter config; the codec
 * stats it holds outlive every per-connection codec.
 */
class Http1HandshakeResponderConfigurator : public ResponderHandshakeConfigurator {
public:
  explicit Http1HandshakeResponderConfigurator(Http::Http1::CodecStats::AtomicPtr& codec_stats)
      : codec_stats_(codec_stats) {}

  ResponderHandshakePtr createHandshake(ResponderHandshakeCallbacks& callbacks) const override;

private:
  Http::Http1::CodecStats::AtomicPtr& codec_stats_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
