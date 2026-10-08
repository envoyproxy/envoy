#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/response_decoder_impl_base.h"
#include "source/common/network/filter_impl.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations.
class ReverseConnectionIOHandle;
class ReverseTunnelInitiatorExtension;

/**
 * Class representing handshake failure with type and context.
 * Provides methods to generate detailed error messages and stat names.
 */
class HandshakeFailureReason {
public:
  enum class Type {
    HttpStatusError, // HTTP response with non-200 status code
    EncodeError,     // HTTP request encoding failed
    Timeout,         // Handshake response not received within the deadline
    ConnectionClose, // Connection closed or the codec failed before the response
  };

  /**
   * Create a handshake failure reason for HTTP status errors.
   * @param status_code the HTTP status code received
   */
  static HandshakeFailureReason httpStatusError(absl::string_view status_code) {
    return {Type::HttpStatusError, status_code};
  }

  /**
   * Create a handshake failure reason for encoding errors.
   */
  static HandshakeFailureReason encodeError() { return {Type::EncodeError, ""}; }

  /**
   * Create a handshake failure reason for a deadline expiry.
   */
  static HandshakeFailureReason timeout() { return {Type::Timeout, ""}; }

  /**
   * Create a handshake failure reason for a connection close or codec failure before the response.
   * @param detail optional context describing the close.
   */
  static HandshakeFailureReason connectionClose(absl::string_view detail = "") {
    return {Type::ConnectionClose, detail};
  }

  /**
   * Get a detailed human-readable error message.
   * @return detailed error message string
   */
  std::string getDetailedName() const {
    switch (type_) {
    case Type::HttpStatusError:
      return absl::StrCat("HTTP handshake failed with status ", context_);
    case Type::EncodeError:
      return "HTTP handshake encode failed";
    case Type::Timeout:
      return "HTTP handshake timed out";
    case Type::ConnectionClose:
      return context_.empty() ? "HTTP handshake connection closed"
                              : absl::StrCat("HTTP handshake connection closed: ", context_);
    }
    return "Unknown handshake failure";
  }

  /**
   * Get the stat name suffix for this failure.
   * @return stat name suffix (e.g., "http.401", "encode_error")
   */
  std::string getNameForStats() const {
    switch (type_) {
    case Type::HttpStatusError:
      return absl::StrCat("http.", context_);
    case Type::EncodeError:
      return "encode_error";
    case Type::Timeout:
      return "timeout";
    case Type::ConnectionClose:
      return "connection_close";
    }
    return "unknown";
  }

private:
  HandshakeFailureReason(Type type, absl::string_view context) : type_(type), context_(context) {}

  Type type_;
  std::string context_;
};

/**
 * Simple read filter for handling reverse connection handshake responses.
 * This filter processes the HTTP response from the upstream server during handshake.
 */
class SimpleConnReadFilter : public Network::ReadFilterBaseImpl,
                             public Logger::Loggable<Logger::Id::main> {
public:
  /**
   * Constructor that stores pointer to parent wrapper.
   */
  explicit SimpleConnReadFilter(void* parent) : parent_(parent) {}

  /**
   * Clear the back-pointer to the owning wrapper. Called from RCConnectionWrapper::shutdown()
   * so a late onData() after teardown cannot dispatch into a destroyed / half-destroyed codec.
   */
  void clearParent() { parent_ = nullptr; }

  // Network::ReadFilter overrides
  Network::FilterStatus onData(Buffer::Instance& buffer, bool end_stream) override;

private:
  void* parent_; // Pointer to RCConnectionWrapper to avoid circular dependency.
};

/**
 * Wrapper for reverse connections that manages the connection lifecycle and handshake.
 * It handles the handshake process (both gRPC and HTTP fallback) and manages connection
 * callbacks and cleanup.
 */
class RCConnectionWrapper : public Network::ConnectionCallbacks,
                            public Event::DeferredDeletable,
                            public Logger::Loggable<Logger::Id::main>,
                            public Http::ResponseDecoderImplBase,
                            public Http::ConnectionCallbacks {
  friend class SimpleConnReadFilterTest;

public:
  /**
   * Constructor for RCConnectionWrapper.
   * @param parent reference to the parent ReverseConnectionIOHandle
   * @param connection the client connection to wrap
   * @param host the upstream host description
   * @param cluster_name the name of the cluster
   */
  RCConnectionWrapper(ReverseConnectionIOHandle& parent, Network::ClientConnectionPtr connection,
                      Upstream::HostDescriptionConstSharedPtr host,
                      const std::string& cluster_name);

  /**
   * Destructor for RCConnectionWrapper.
   * Performs defensive cleanup to prevent crashes during shutdown.
   */
  ~RCConnectionWrapper() override;

  // Network::ConnectionCallbacks overrides
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Http::ResponseDecoder overrides
  void decode1xxHeaders(Http::ResponseHeaderMapPtr&&) override {}
  void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(Http::ResponseTrailerMapPtr&&) override {}
  void decodeMetadata(Http::MetadataMapPtr&&) override {}
  void dumpState(std::ostream&, int) const override {}

  // Http::ConnectionCallbacks overrides
  void onGoAway(Http::GoAwayErrorCode) override {}
  void onSettings(Http::ReceivedSettings&) override {}
  void onMaxStreamsChanged(uint32_t) override {}

  /**
   * Initiate the reverse connection handshake (HTTP only).
   * @param src_tenant_id the tenant identifier
   * @param src_cluster_id the cluster identifier
   * @param src_node_id the node identifier
   * @param initiation_time_ms epoch millis to advertise as the tunnel's initiation time. When
   *        absent, the current system time is used.
   * @return ``absl::OkStatus()`` when the handshake request was dispatched, or an error status when
   *         it failed synchronously. On a synchronous failure onConnectionDone has already run, so
   *         the caller treats the attempt as terminal.
   */
  absl::Status connect(const std::string& src_tenant_id, const std::string& src_cluster_id,
                       const std::string& src_node_id,
                       std::optional<int64_t> initiation_time_ms = std::nullopt);

  /**
   * Release ownership of the connection.
   * @return the connection pointer (ownership transferred to caller)
   */
  Network::ClientConnectionPtr releaseConnection();

  /**
   * Process HTTP response from upstream.
   * @param buffer the response data
   * @param end_stream whether this is the end of the stream
   */
  void processHttpResponse(Buffer::Instance& buffer, bool end_stream);

  /**
   * Handle successful handshake completion.
   */
  void onHandshakeSuccess();

  /**
   * Handle handshake failure.
   * @param reason the failure reason with type and context
   * @param retry_after optional server cool-off hint parsed from a ``Retry-After`` header on a 429
   *        response; forwarded to the parent to drive the per-host backoff.
   */
  void onHandshakeFailure(const HandshakeFailureReason& reason,
                          std::optional<std::chrono::milliseconds> retry_after = std::nullopt);

  /**
   * Parse an HTTP ``Retry-After`` header into a cool-off duration. Only the RFC 7231 delta-seconds
   * form is supported (the form rate limiters emit); the HTTP-date form, a zero value, and
   * malformed/absent values return ``nullopt`` so the caller falls back to its computed backoff.
   * @param headers the response headers to inspect.
   * @return the parsed cool-off duration, or ``nullopt`` if not present/parseable/zero.
   */
  static std::optional<std::chrono::milliseconds>
  parseRetryAfter(const Http::ResponseHeaderMap& headers);

  /**
   * Perform graceful shutdown of the connection.
   */
  void shutdown();

  /**
   * Get the underlying connection.
   * @return pointer to the client connection
   */
  Network::ClientConnection* getConnection() { return connection_.get(); }

  /**
   * Get the host description.
   * @return shared pointer to the host description
   */
  Upstream::HostDescriptionConstSharedPtr getHost() { return host_; }

  /**
   * Release any bytes the responder coalesced with the handshake response. These are carried with
   * the tunnel so the accepted handle can replay them before reading the socket.
   * @return the residual buffer, or nullptr when nothing was coalesced.
   */
  Buffer::InstancePtr takeHandshakeResidual() { return std::move(handshake_residual_); }

private:
  // Fails the handshake when the response deadline expires.
  void onHandshakeTimeout();

  // Cancels the handshake deadline without destroying the timer, which is unsafe from the timer's
  // own fire callback. The timer is freed with the wrapper.
  void disarmHandshakeTimer();

  // Finalizes a successful handshake after dispatch returns so residual bytes coalesced with the
  // response are captured before the tunnel is queued.
  void completeHandshakeHandoff();

  // Moves any bytes remaining in ``buffer`` into handshake_residual_, allocating it on first use.
  void captureHandshakeResidual(Buffer::Instance& buffer);

  ReverseConnectionIOHandle& parent_;
  Network::ClientConnectionPtr connection_;
  Upstream::HostDescriptionConstSharedPtr host_;
  std::string cluster_name_;
  std::string connection_key_;
  bool http_handshake_sent_{false};
  bool handshake_completed_{false};
  bool shutdown_called_{false};

  // Set when the handshake succeeds so the handoff is finalized after dispatch returns.
  bool pending_handoff_{false};

  // Deadline for receiving the handshake response. Disabled on success, failure, or shutdown.
  Event::TimerPtr handshake_timer_;

  // Bytes the responder coalesced with the handshake response (for example the HTTP/2 preface).
  // Null until the first such byte is captured, then carried with the tunnel.
  Buffer::InstancePtr handshake_residual_;

  /**
   * Get the downstream extension for accessing stats.
   * @return pointer to ReverseTunnelInitiatorExtension
   */
  ReverseTunnelInitiatorExtension* getDownstreamExtension() const;

public:
  // Dispatch incoming bytes to HTTP/1 codec.
  void dispatchHttp1(Buffer::Instance& buffer);

private:
  // HTTP/1 codec used to send request and parse response.
  std::unique_ptr<Http::Http1::ClientConnectionImpl> http1_client_codec_;
  // Base interface pointer used to call dispatch via public API.
  Http::Connection* http1_parse_connection_{nullptr};
  std::shared_ptr<SimpleConnReadFilter> read_filter_{std::make_shared<SimpleConnReadFilter>(this)};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
