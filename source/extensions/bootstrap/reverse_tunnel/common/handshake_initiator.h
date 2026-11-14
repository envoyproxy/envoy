#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/upstream.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * A single formatted handshake header: literal key, an append action, and a value formatter
 * evaluated against an empty stream info at handshake time.
 */
struct HandshakeHeader {
  Http::LowerCaseString key;
  envoy::config::core::v3::HeaderValueOption::HeaderAppendAction append_action;
  Formatter::FormatterPtr value_formatter;
};
using HandshakeHeadersConstSharedPtr = std::shared_ptr<const std::vector<HandshakeHeader>>;

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
 * Terminal outcome of a handshake attempt, reported to the owning io handle.
 */
struct HandshakeOutcome {
  bool accepted{false};
  // Typed failure classification, set only when accepted is false. Transport-level failures that
  // never produced a handshake response (e.g. a close before the response) may leave it unset.
  std::optional<HandshakeFailureReason> failure_reason;
  // Human-readable outcome message used in logs.
  std::string message;

  static HandshakeOutcome success() {
    return HandshakeOutcome{true, std::nullopt, "reverse connection accepted"};
  }
  static HandshakeOutcome failure(HandshakeFailureReason reason) {
    HandshakeOutcome outcome;
    outcome.failure_reason = std::move(reason);
    outcome.message = outcome.failure_reason->getDetailedName();
    return outcome;
  }
  // Message-only failures classify as transport close for stats, so every terminal failure is
  // counted; the message stays verbatim for logs.
  static HandshakeOutcome failure(absl::string_view message) {
    HandshakeOutcome outcome;
    outcome.failure_reason = HandshakeFailureReason::connectionClose(message);
    outcome.message = std::string(message);
    return outcome;
  }
};

/**
 * Sink through which a per-connection initiator handshake reaches the dial it belongs to.
 * Implemented by RCConnectionWrapper; all methods run on the worker that owns the connection,
 * between handshake creation and terminal outcome.
 */
class InitiatorHandshakeCallbacks {
public:
  virtual ~InitiatorHandshakeCallbacks() = default;

  // Connection the handshake runs on; valid until the handshake completes or is cancelled.
  virtual Network::ClientConnection& connection() = 0;

  // Host description of the dialed endpoint; carries the codec settings and limits.
  virtual Upstream::HostDescriptionConstSharedPtr host() = 0;

  // Name of the upstream cluster the tunnel is dialed for.
  virtual const std::string& clusterName() = 0;

  // Identity of this Envoy instance, carried on the handshake request.
  virtual const std::string& srcTenantId() = 0;
  virtual const std::string& srcClusterId() = 0;
  virtual const std::string& srcNodeId() = 0;

  // Handshake request configuration.
  virtual const std::string& requestPath() = 0;
  virtual bool useHttpUpgrade() = 0;
  virtual const std::vector<envoy::config::core::v3::HeaderValueOption>& additionalHeaders() = 0;

  // Handshake headers to send on the request. Resolved live at start() time — the value can change
  // between dials — so implementations must call this once per start() and never cache it.
  virtual HandshakeHeadersConstSharedPtr effectiveHandshakeHeaders() = 0;

  // Called when the handshake response is classified as success, before the handshake hands the
  // connection over. Bytes the responder coalesced with the response are still in flight; the
  // terminal notification arrives via onHandshakeDone once they are captured.
  virtual void onHandshakeSucceeded() = 0;

  // Reports the terminal outcome. Called exactly once per handshake, after which the handshake
  // makes no further callbacks.
  virtual void onHandshakeDone(HandshakeOutcome outcome,
                               std::optional<std::chrono::milliseconds> retry_after) = 0;
};

/**
 * Per-connection initiator handshake. One instance per dial attempt; drives the protocol-specific
 * half of establishing a reverse tunnel. Implementations must only interact with the transport
 * through the connection exposed by the callbacks.
 */
class InitiatorHandshake {
public:
  virtual ~InitiatorHandshake() = default;

  /**
   * Sends the handshake request on the connection. Called once, from the worker that owns the
   * connection.
   * @param initiation_time_ms epoch millis advertised as the tunnel's initiation time; absent
   *        means the current time.
   * @return ok when the request was dispatched. On a synchronous failure the terminal outcome has
   *         already been reported, so the caller treats the attempt as terminal.
   */
  virtual absl::Status start(std::optional<int64_t> initiation_time_ms) = 0;

  /**
   * Feeds received bytes into the handshake. Called only from the worker that owns the
   * connection, after start(). Bytes past the handshake response are captured as residual and
   * replayed to the accepted tunnel; they are never interpreted as handshake input.
   */
  virtual void onData(Buffer::Instance& buffer) = 0;

  /**
   * Stops the handshake without a terminal outcome (wrapper teardown or transport close) and
   * detaches from the connection. Any residual captured so far stays readable via takeResidual().
   * No callback runs after cancel().
   */
  virtual void cancel() = 0;

  /**
   * Returns the bytes the responder coalesced with the handshake response, or nullptr when none.
   * Carried with the tunnel so the accepted handle can replay them before reading the socket.
   */
  virtual Buffer::InstancePtr takeResidual() = 0;
};

/**
 * Creates per-connection initiator handshakes. One factory serves all dial attempts of an
 * initiator; it is immutable after construction.
 */
class InitiatorHandshakeConfigurator {
public:
  virtual ~InitiatorHandshakeConfigurator() = default;

  virtual std::unique_ptr<InitiatorHandshake>
  createHandshake(InitiatorHandshakeCallbacks& callbacks) const = 0;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
