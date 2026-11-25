#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "envoy/buffer/buffer.h"
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
  // Typed failure classification; both failure() factories set it.
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
  // Classifies as a connection close; the message stays verbatim for logs.
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
 * between handshake creation and terminal outcome. Carries only identity, transport and outcome;
 * protocol-specific settings live in the handshake's own factory.
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

  // Reports the terminal outcome and ends the attempt: the deadline is disarmed and the outcome is
  // counted. Called at most once per handshake, after which the handshake makes no further
  // callbacks. On success, bytes the responder coalesced with the response must already be
  // readable via takeResidual(). A success re-enters cancel() and takeResidual() before this
  // returns, so report it only after the codec dispatch returns, never from inside a codec
  // callback that still uses the codec. A failure may be reported from inside a protocol callback
  // (HTTP/1 reports it from decodeHeaders()), so the owner defers destroying the handshake until
  // that callback returns. A retry_after below one second is ignored and the computed backoff
  // applies.
  virtual void onHandshakeDone(HandshakeOutcome outcome,
                               std::optional<std::chrono::milliseconds> retry_after) = 0;
};

/**
 * Per-connection initiator handshake. One instance per dial attempt; drives the protocol-specific
 * half of establishing a reverse tunnel. Implementations must only interact with the transport
 * through the connection exposed by the callbacks.
 *
 * Lifetime: the connection stays valid until the terminal outcome or cancel(). The owner may
 * destroy the handshake after either, deferring it past any protocol callback still on the stack,
 * so no callback may be pending past them.
 */
class InitiatorHandshake {
public:
  virtual ~InitiatorHandshake() = default;

  /**
   * Sends the handshake request on the connection. Called once, from the worker that owns the
   * connection.
   * @param initiation_time_ms epoch millis advertised as the tunnel's initiation time; absent
   *        means the current time.
   * @return ok when the request was dispatched and the attempt is still in flight. start() never
   *         reports success; a failure reported from within it must come with a non-ok status.
   */
  virtual absl::Status start(std::optional<int64_t> initiation_time_ms) = 0;

  /**
   * Feeds bytes received on the connection into the handshake. The owner delivers every read
   * after start() until the terminal outcome or cancel(), on the worker that owns the connection.
   * Bytes past the handshake response are captured as residual and replayed to the accepted
   * tunnel; they are never interpreted as handshake input.
   */
  virtual void onData(Buffer::Instance& buffer) = 0;

  /**
   * Stops the handshake without a terminal outcome (wrapper teardown, deadline, or a success
   * handoff) and detaches from the connection. A transport close reaches it only through the
   * owner's deferred teardown. It may run more than once and after the terminal outcome, for
   * example after a failure and twice after a deadline. Any residual captured so far stays
   * readable via takeResidual(). No callback runs after cancel().
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
 * initiator. Its configuration is fixed at construction, except state the extension publishes
 * once on the main thread before workers dial, such as the HTTP/1 formatted headers.
 */
class InitiatorHandshakeConfigurator {
public:
  virtual ~InitiatorHandshakeConfigurator() = default;

  // Never returns null.
  virtual std::unique_ptr<InitiatorHandshake>
  createHandshake(InitiatorHandshakeCallbacks& callbacks) const = 0;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
