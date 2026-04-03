#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

/**
 * A handle to a split request.
 */
class SplitRequest {
public:
  virtual ~SplitRequest() = default;

  /**
   * Cancel the request. No further request callbacks will be called.
   */
  virtual void cancel() PURE;
};

using SplitRequestPtr = std::unique_ptr<SplitRequest>;

/**
 * Split request callbacks.
 */
class SplitCallbacks {
public:
  virtual ~SplitCallbacks() = default;

  /**
   * Called to verify that commands should be processed.
   * @return bool true if commands from this client connection can be processed, false if not.
   */
  virtual bool connectionAllowed() PURE;

  /**
   * Called when a quit command has been received.
   */
  virtual void onQuit() PURE;

  /**
   * Called when an authentication command has been received with a password.
   * @param password supplies the AUTH password provided by the downstream client.
   */
  virtual void onAuth(const std::string& password) PURE;

  /**
   * Called when an authentication command has been received with a username and password.
   * @param username supplies the AUTH username provided by the downstream client.
   * @param password supplies the AUTH password provided by the downstream client.
   */
  virtual void onAuth(const std::string& username, const std::string& password) PURE;

  /**
   * Called when the response is ready.
   * @param value supplies the response which is now owned by the callee.
   */
  virtual void onResponse(Common::Redis::RespValuePtr&& value) PURE;

  /**
   * Called to retrieve information about the current Redis transaction.
   * @return reference to a Transaction instance of the current connection.
   */
  virtual Common::Redis::Client::Transaction& transaction() PURE;

  /**
   * Result of an inline auth check.
   *   Allowed - credentials are valid; connection_allowed_ has been set. The splitter
   *             emits the HELLO Map for the requested protocol version.
   *   Denied  - credentials are invalid. The splitter emits ``WRONGPASS``.
   *   Pending - external auth is in flight (async gRPC round trip). The splitter emits
   *             nothing; the implementation behind ``attemptDownstreamAuthInline`` is
   *             responsible for emitting the deferred HELLO Map (success) or error
   *             (failure) for the supplied ``requested_version`` and for setting the
   *             downstream RESP version on success when the round trip resolves.
   */
  enum class AuthAttempt { Allowed, Denied, Pending };

  /**
   * Validate downstream credentials inline as part of HELLO negotiation. Used by HELLO when
   * the client provides AUTH options on the same command:
   *
   *   HELLO N AUTH <user> <pass> [SETNAME <name>]
   *
   * The HELLO handler must produce a single reply (HELLO Map on success, error on failure),
   * so it cannot use ``onAuth`` (which emits its own response).
   *
   * When external auth is configured, the implementation kicks off the async authentication
   * round trip and returns ``Pending``; when the result arrives the implementation is
   * responsible for emitting the final HELLO Map (or error) for the supplied
   * ``requested_version`` and for setting the downstream RESP version on success. The
   * splitter does not emit any response in the Pending case.
   *
   * @param username inline AUTH username (empty string if HELLO carried no AUTH options).
   * @param password inline AUTH password.
   * @param requested_version the RESP protocol version the client requested in HELLO. The
   *        implementation needs this to construct the deferred HELLO reply when it is the one
   *        emitting it (Pending path). For Allowed/Denied the splitter emits using the same
   *        version itself.
   * @return Allowed: local credentials match — splitter emits HELLO Map for requested_version.
   *         Denied: local credentials do not match — splitter emits WRONGPASS.
   *         Pending: external-auth round trip in flight — splitter emits nothing; the
   *           implementation will emit HELLO Map (or error) when the round trip completes.
   */
  virtual AuthAttempt attemptDownstreamAuthInline(const std::string& username,
                                                  const std::string& password,
                                                  uint32_t requested_version) PURE;

  /**
   * Called when HELLO negotiation succeeds to record the downstream protocol version.
   * @param version the RESP protocol version (2 or 3).
   */
  virtual void setDownstreamRespVersion(uint32_t version) PURE;

  /**
   * @return the maximum RESP version a downstream client may negotiate via
   * ``HELLO N`` on this connection. ``HELLO N`` with ``N`` above this value
   * is rejected with ``-NOPROTO``. The default of ``2`` matches a connection
   * served by a RESP2-only backend; concrete implementations source the
   * value from per-listener configuration (see
   * ``ProxyFilterConfig::clusterRespVersion``).
   */
  virtual uint32_t clusterRespVersion() const { return 2; }

  /**
   * Current downstream RESP version negotiated on this connection.
   * Used by HELLO handling to synthesize a reply that matches the
   * client's current view of the protocol (in case of bare HELLO) and then
   * flip to the new version.
   */
  virtual uint32_t currentDownstreamRespVersion() const { return 2; }

  /**
   * Schedule a downstream connection close after the in-flight response
   * has flushed to the wire. Used when the upstream cluster's RESP cap has
   * dropped below the connection's negotiated downstream version (e.g. CDS
   * downgraded the cluster RESP3 → RESP2 under an already-negotiated
   * downstream connection): the proxy can no longer honor the negotiated
   * RESP3 contract, so the safe action is to send -NOPROTO and close so the
   * client reconnects and renegotiates against the new (lower) cap.
   */
  virtual void closeDownstreamAfterResponse() {}
};

/**
 * Build the HELLO command reply (Map for a RESP3 downstream; the encoder converts to a flat
 * array on a RESP2 downstream) for the given negotiated protocol version. Exposed so
 * ``ProxyFilter`` can emit a deferred HELLO reply after an external-auth round trip completes
 * for ``HELLO N AUTH <user> <pass>`` — the splitter's HELLO handler returns control before
 * the reply is built in that case.
 */
Common::Redis::RespValuePtr buildHelloReply(uint32_t downstream_version);

/**
 * A command splitter that takes incoming redis commands and splits them as appropriate to a
 * backend connection pool.
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   * Make a split redis request capable of being retried/redirected.
   * @param request supplies the split request to make (ownership transferred to call).
   * @param callbacks supplies the split request completion callbacks.
   * @param dispatcher supplies dispatcher used for delay fault timer.
   * @param stream_info reference to the stream info used for formatting the key.
   * @return SplitRequestPtr a handle to the active request or nullptr if the request is no
   *         longer in the splitter's hands. ``nullptr`` covers two cases: (1) the splitter
   *         has already satisfied the request synchronously via ``onResponse()`` (the common
   *         case for HELLO responses, locally-validated AUTH, etc.); (2) the splitter has
   *         deferred the response to an out-of-band path that the implementing
   *         ``SplitCallbacks`` will complete (currently: HELLO N AUTH ... routed to an
   *         external auth provider — see ``attemptDownstreamAuthInline`` returning
   *         ``Pending``). In both cases the caller's ``SplitCallbacks`` will eventually be
   *         notified, but in case (2) the notification arrives via a separate code path
   *         (e.g. ``ProxyFilter::onAuthenticateExternal``) rather than from the splitter
   *         itself.
   */
  virtual SplitRequestPtr makeRequest(Common::Redis::RespValuePtr&& request,
                                      SplitCallbacks& callbacks, Event::Dispatcher& dispatcher,
                                      const StreamInfo::StreamInfo& stream_info) PURE;
};

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
