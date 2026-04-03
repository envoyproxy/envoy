#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/codec.h"
#include "source/extensions/filters/network/redis_proxy/subscription_registry.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

// Terminal response payload handed to SplitCallbacks::respond(). The overwhelmingly common case is
// a single reply frame, so an InlinedVector<,1> keeps that frame inline and avoids a per-command
// heap allocation on ALL traffic (multi-frame pub/sub acks spill to the heap, as a vector would)
// (E-1).
using RespValueFrames = absl::InlinedVector<Common::Redis::RespValuePtr, 1>;

struct ResponseValues {
  const std::string OK = "OK";
  const std::string InvalidRequest = "invalid request";
  const std::string NoUpstreamHost = "no upstream host";
  const std::string UpstreamFailure = "upstream failure";
  const std::string UpstreamProtocolError = "upstream protocol error";
  const std::string AuthRequiredError = "NOAUTH Authentication required.";
  const std::string UnsupportedProtocol = "NOPROTO unsupported protocol version";
};

using Response = ConstSingleton<ResponseValues>;

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
 * Pub/sub session hooks. Split out of SplitCallbacks because only the pub/sub command handlers
 * touch subscriber/registry state and only the ProxyFilter's PendingRequest implements them —
 * every other SplitCallbacks (auth flows, speed-test/no-op doubles) carries no subscription
 * state. Reached via SplitCallbacks::pubsub(), which returns nullptr on connections that never
 * subscribe.
 */
class PubsubSession {
public:
  virtual ~PubsubSession() = default;

  /**
   * Get or create the downstream subscriber for pub/sub.
   */
  virtual DownstreamSubscriberPtr downstreamSubscriber() PURE;

  /**
   * Track a subscription registry for cleanup on disconnect.
   */
  virtual void setSubscriptionRegistry(const SubscriptionRegistryPtr& registry) PURE;

  /**
   * Unsubscribe ``subscriber`` from a single ``channel`` across every subscription registry this
   * connection tracks (routing may have moved the channel since the original SUBSCRIBE), buffering
   * any preserved ``subscribe`` ack (Issue 4) into ``preserved_acks`` for the splitter to flush
   * after its terminal respond(). Owns the cross-registry ownership rule — a channel lives in
   * exactly one registry, so the scan stops at the first that actually drops the subscriber's count
   * — so the splitter never walks trackedRegistries() itself (A-8: cross-registry semantics belong
   * to the session, the splitter only parses the verb).
   * @return the subscriber's total subscription count after the unsubscribe (the downstream ack
   *         count; unchanged from before when no tracked registry owned the channel).
   */
  virtual uint64_t
  unsubscribeChannelAcrossRegistries(const std::string& channel,
                                     const DownstreamSubscriberPtr& subscriber,
                                     std::vector<Common::Redis::RespValue>& preserved_acks) PURE;

  /**
   * Report a net pub/sub subscription-count change for the cumulative subscribe/unsubscribe event
   * COUNTERS (``pubsub_subscribe_total`` / ``pubsub_unsubscribe_total``). The
   * ``pubsub_active_subscriptions`` GAUGE is NOT touched here — it is owned solely by
   * DownstreamSubscriber::addChannel/removeChannel and is correct by construction (A-2).
   */
  virtual void onPubsubSubscriptionChange(int64_t delta) PURE;
};

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
   * Terminal response: hand the pending request its complete, ordered list of downstream frames in
   * one call. The request buffers all frames and marks itself complete, then flushes when it
   * reaches the FIFO front. Frame count reflects the command: exactly one for the overwhelmingly
   * common single-reply case (see the onResponse sugar below), several for a multi-channel
   * ``SUBSCRIBE`` whose per-channel ``-ERR`` replies share one FIFO entry, or zero for a request
   * whose acks all flowed out-of-band via DownstreamSubscriber::deliver (bare ``UNSUBSCRIBE`` with
   * no channels). Push frames + fabricated subscribe acks intentionally bypass the FIFO via
   * DownstreamSubscriber::deliver (out-of-band per RESP3); ordinary replies flow through here.
   * respond() is terminal — it may pop *this from the FIFO, so callers must not touch the callbacks
   * afterward.
   * @param frames the ordered response frames, now owned by the callee.
   */
  virtual void respond(RespValueFrames&& frames) PURE;

  /**
   * Sugar for the overwhelmingly common single-frame terminal response: forwards to respond() with
   * a one-element list. Non-virtual on purpose — respond() is the sole virtual terminal, so the
   * ~55 single-reply splitter call sites plus the test drivers keep calling onResponse() unchanged.
   * @param value supplies the response which is now owned by the callee.
   */
  void onResponse(Common::Redis::RespValuePtr&& value) {
    RespValueFrames frames;
    frames.push_back(std::move(value));
    respond(std::move(frames));
  }

  /**
   * Called to retrieve information about the current Redis transaction.
   * @return reference to a Transaction instance of the current connection.
   */
  virtual Common::Redis::Client::Transaction& transaction() PURE;

  /**
   * Result of an inline auth check.
   *   Allowed - credentials are valid; connection_allowed_ has been set. The splitter emits the
   *     HELLO Map for the requested protocol version.
   *   Denied - credentials are invalid. The splitter emits ``WRONGPASS``.
   *   ImplOwnsResponse - the implementation owns the response; the splitter emits nothing.
   *     Either external auth is in flight (async gRPC round trip; the impl later emits the
   *     deferred HELLO Map / error and sets the downstream RESP version on success), or the impl
   *     already emitted a final error synchronously (e.g. HELLO AUTH with no downstream
   *     credentials configured). The name states the invariant, not timing: in the synchronous
   *     case nothing is "pending", yet the impl still owns the already-emitted response.
   */
  enum class AuthAttempt { Allowed, Denied, ImplOwnsResponse };

  /**
   * Validate downstream credentials inline as part of HELLO negotiation. Used by HELLO when
   * the client provides AUTH options on the same command:
   *
   *   HELLO N AUTH <user> <pass> [SETNAME <name>]
   *
   * The HELLO handler must produce a single reply (HELLO Map on success, error on failure),
   * so it cannot use onAuth (which emits its own response).
   *
   * When external auth is configured, the implementation kicks off the async authentication
   * round trip and returns ``ImplOwnsResponse``; when the result arrives the implementation is
   * responsible for emitting the final HELLO Map (or error) for the supplied
   * ``requested_version`` and for setting the downstream RESP version on success. The
   * splitter does not emit any response in the ImplOwnsResponse case.
   *
   * @param username inline AUTH username (empty string if HELLO carried no AUTH options).
   * @param password inline AUTH password.
   * @param requested_version the RESP protocol version the client requested in HELLO. The
   *        implementation needs this to construct the deferred HELLO reply when it is the one
   *        emitting it (ImplOwnsResponse path). For Allowed/Denied the splitter emits using the
   *        same version itself.
   * @return Allowed: local credentials match — splitter emits HELLO Map for requested_version.
   *         Denied: local credentials do not match — splitter emits WRONGPASS.
   *         ImplOwnsResponse: splitter emits nothing; the implementation owns the response. Either
   *           an external-auth round trip is in flight (the impl emits HELLO Map / error on
   *           completion) or the impl has already emitted a final error synchronously (e.g.
   *           HELLO AUTH with no downstream credentials configured).
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
   * Listener-level RESP version. ``HELLO N`` is accepted only when N matches (wire 2 / 3);
   * pre-HELLO data commands on a ``Resp3`` listener are rejected.
   */
  virtual Common::Redis::RespProtocolVersion protocolVersion() const PURE;

  /**
   * Whether this listener opted into sharded pub/sub publish (``enable_sharded_publish``).
   * When true, ``PublishRequest`` rewrites a client ``PUBLISH`` to upstream ``SPUBLISH`` so it
   * reaches the slot-owning shard; when false, classic ``PUBLISH`` is forwarded unchanged.
   * Defaults to false so non-pub/sub callbacks and test doubles keep classic behavior.
   */
  virtual bool shardedPublishEnabled() const { return false; }

  /**
   * Current downstream RESP version negotiated on this connection. The HELLO handler
   * inherits this as the requested version on bare ``HELLO`` (no version arg). The actual
   * version flip on a successful ``HELLO N`` is performed by ``setDownstreamRespVersion``.
   * Also used by the pub/sub handler to gate SUBSCRIBE acceptance on a RESP3-negotiated
   * connection.
   */
  virtual uint32_t currentDownstreamRespVersion() const PURE;

  /**
   * Consume the pending ``HELLO N`` version stashed when an inline ``HELLO N AUTH ...`` was
   * deferred to an external auth provider (see ``attemptDownstreamAuthInline`` returning
   * ImplOwnsResponse). Returns the requested version and clears the stash; ``std::nullopt`` when
   * the deferred auth came from a stand-alone ``AUTH`` command rather than ``HELLO``. Lets the
   * external-auth completion path build the right reply (HELLO Map vs +OK) without
   * downcasting the callback to a concrete implementation type.
   */
  virtual std::optional<uint32_t> takePendingHelloAuthVersion() PURE;

  /**
   * Pub/sub session for this connection, or nullptr on connections that never subscribe (auth
   * flows, non-pub/sub commands, test/no-op doubles). Only the ProxyFilter's PendingRequest returns
   * a real session; the pub/sub command handlers reach subscriber/registry state through it.
   */
  virtual PubsubSession* pubsub() { return nullptr; }
};

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
   *         longer in the splitter's hands. nullptr covers two cases: (1) the splitter
   *         has already satisfied the request synchronously via ``onResponse()`` (the common
   *         case for HELLO responses, locally-validated AUTH, etc.); (2) the splitter has
   *         deferred the response to an out-of-band path that the implementing
   *         ``SplitCallbacks`` will complete (currently: HELLO N AUTH ... routed to an
   *         external auth provider — see ``attemptDownstreamAuthInline`` returning
   *         ``ImplOwnsResponse``). In both cases the caller's ``SplitCallbacks`` will
   *         eventually be notified, but in case (2) the notification arrives via a separate
   *         code path (e.g. ``ProxyFilter::onAuthenticateExternal``) rather than from the
   *         splitter itself.
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
