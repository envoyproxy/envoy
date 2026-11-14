#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/response_decoder_impl_base.h"
#include "source/common/network/filter_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/handshake_initiator.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Read filter that feeds received bytes into the owning HTTP/1 handshake's codec.
 */
class SimpleConnReadFilter : public Network::ReadFilterBaseImpl,
                             public Logger::Loggable<Logger::Id::main> {
public:
  explicit SimpleConnReadFilter(void* parent) : parent_(parent) {}

  /**
   * Clear the back-pointer to the owning handshake. Called from cancel() so a late onData() after
   * teardown cannot dispatch into a destroyed / half-destroyed codec.
   */
  void clearParent() { parent_ = nullptr; }

  // Network::ReadFilter overrides
  Network::FilterStatus onData(Buffer::Instance& buffer, bool end_stream) override;

private:
  void* parent_; // Pointer to Http1HandshakeInitiator to avoid circular dependency.
};

/**
 * HTTP/1.1 initiator handshake: sends the reverse tunnel request and classifies the response.
 * One instance per dial attempt.
 */
class Http1HandshakeInitiator : public InitiatorHandshake,
                                public Http::ResponseDecoderImplBase,
                                public Http::ConnectionCallbacks,
                                public Logger::Loggable<Logger::Id::main> {
public:
  explicit Http1HandshakeInitiator(InitiatorHandshakeCallbacks& callbacks);

  // InitiatorHandshake overrides
  absl::Status start(std::optional<int64_t> initiation_time_ms) override;
  void onData(Buffer::Instance& buffer) override;
  void cancel() override;
  Buffer::InstancePtr takeResidual() override;

  // Http::ResponseDecoder overrides
  void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&&) override {}
  void decodeMetadata(Http::MetadataMapPtr&&) override {}
  void dumpState(std::ostream&, int) const override {}

  // Http::ConnectionCallbacks
  void onGoAway(Http::GoAwayErrorCode) override {}

  /**
   * Parse an HTTP ``Retry-After`` header into a cool-off duration. Only the RFC 7231 delta-seconds
   * form is supported (the form rate limiters emit); the HTTP-date form, a zero value, and
   * malformed/absent values return ``nullopt`` so the caller falls back to its computed backoff.
   * @param headers the response headers to inspect.
   * @return the parsed cool-off duration, or ``nullopt`` if not present/parseable/zero.
   */
  static std::optional<std::chrono::milliseconds>
  parseRetryAfter(const Http::ResponseHeaderMap& headers);

private:
  // Dispatches received bytes to the HTTP/1 codec and completes a pending handoff once dispatch
  // returns, so residual bytes coalesced with the response are captured before the tunnel is
  // queued.
  void dispatchHttp1(Buffer::Instance& buffer);

  // Moves any bytes remaining in ``buffer`` into handshake_residual_, allocating it on first use.
  void captureHandshakeResidual(Buffer::Instance& buffer);

  InitiatorHandshakeCallbacks& callbacks_;
  // HTTP/1 codec used to send request and parse response.
  std::unique_ptr<Http::Http1::ClientConnectionImpl> http1_client_codec_;
  // Base interface pointer used to call dispatch via public API.
  Http::Connection* http1_parse_connection_{nullptr};
  // Set when the handshake succeeds so the handoff runs after dispatch returns.
  bool pending_handoff_{false};
  // Guards the exactly-once contract of onHandshakeDone: after the terminal notification, late
  // codec events (extraneous bytes after a success, a second response after a failure) are
  // dropped.
  bool terminal_reported_{false};
  // Bytes the responder coalesced with the handshake response (for example the HTTP/2 preface).
  // Null until the first such byte is captured, then carried with the tunnel.
  Buffer::InstancePtr handshake_residual_;
  std::shared_ptr<SimpleConnReadFilter> read_filter_{std::make_shared<SimpleConnReadFilter>(this)};
};

/**
 * Factory producing HTTP/1.1 initiator handshakes.
 */
class Http1HandshakeConfigurator : public InitiatorHandshakeConfigurator {
public:
  std::unique_ptr<InitiatorHandshake>
  createHandshake(InitiatorHandshakeCallbacks& callbacks) const override {
    return std::make_unique<Http1HandshakeInitiator>(callbacks);
  }
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
