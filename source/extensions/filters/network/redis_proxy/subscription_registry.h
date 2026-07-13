#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"
#include "source/extensions/filters/network/redis_proxy/control_command_ledger.h"
#include "source/extensions/filters/network/redis_proxy/subscribe_ack_deadline_scheduler.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * UAF-safe ordering bridge from a shared DownstreamSubscriber back to its per-connection
 * ProxyFilter. A subscriber is shared (the registry indexes it for fan-out) and can briefly outlive
 * its ProxyFilter during connection teardown, so it holds only a weak_ptr to this sink. The filter
 * owns the sole shared_ptr and detaches (nulls its back-pointer) in its destructor, so a late
 * delivery degrades to a direct connection write instead of dereferencing a freed filter.
 */
class PushOrderingSink {
public:
  virtual ~PushOrderingSink() = default;

  // Hand an already-RESP3-encoded MESSAGE push frame to the filter so it can be flushed at its
  // correct position relative to in-flight command replies (FIFO ordering). The sink MOVES the
  // bytes out of ``encoded`` (leaving it empty on return) rather than copying: every caller
  // (deliverMessage, deliverSharedFrame) owns a throwaway encode buffer, so there is a single
  // ownership-transfer overload (S-3 — the former copying const& overload had no callers once E-2
  // routed fan-out through per-subscriber buffers). ``encoded`` is always empty on return, even if
  // the push is dropped because the filter is gone.
  virtual void enqueueOrderedPush(Buffer::Instance& encoded) PURE;
};

/**
 * The four pub/sub telemetry stats a DownstreamSubscriber owns. Bundled and passed by reference so
 * they are mandatory at construction (S-4): the former per-stat nullable pointers were a test-only
 * default that kept a second, silently stat-less code path alongside production. Grouping them also
 * keeps the ctor to one cohesive argument instead of four positional pointers.
 */
struct DownstreamSubscriberStats {
  Stats::Counter& push_delivered;
  Stats::Gauge& active_subscriptions;
  Stats::Counter& subscribe_ack_success;
  Stats::Counter& subscribe_ack_error;
};

/**
 * Represents a downstream client with active subscriptions.
 */
class DownstreamSubscriber {
public:
  DownstreamSubscriber(Network::Connection& connection, const DownstreamSubscriberStats& stats);
  ~DownstreamSubscriber();

  void deliver(const Common::Redis::RespValue& message);

  // Skeleton-reuse ack delivery (E-6): appendAck ENCODES one control ack into the reused batch
  // buffer (no write, no count — G10) and flushAckBatch writes the whole accumulated batch ONCE.
  // Lets the splitter mutate-and-reuse a single ack skeleton across a multi-channel
  // SUBSCRIBE/UNSUBSCRIBE instead of building one RespValue tree per channel, then flush after its
  // terminal respond() — preserving the out-of-band-after-reply ordering deliverBatch gave (Issue
  // 4): only the write is deferred; the encode is side-effect-free. flushAckBatch always leaves the
  // buffer empty (drained even if the connection closed mid-batch) so no stale bytes carry over.
  void appendAck(const Common::Redis::RespValue& ack);
  void flushAckBatch();

  // Deliver a pub/sub MESSAGE push (message/smessage fan-out). Unlike deliver(), which writes
  // straight to the connection for out-of-band subscribe/unsubscribe acks, a message
  // push must not overtake an in-flight ordinary command reply on a client that is both subscribed
  // and issuing commands (self-``PUBLISH`` being the canonical case). When an ordering sink is
  // wired (production; the owning ProxyFilter injects it) the encoded frame is routed through it
  // for FIFO placement behind pending replies; without a sink (registry unit tests, or a subscriber
  // whose filter is gone) it falls back to a direct connection write, matching legacy behavior.
  void deliverMessage(const Common::Redis::RespValue& message);

  // Fan-out delivery of a pre-encoded MESSAGE frame shared across every subscriber of a channel
  // (E-2). ``bytes`` holds the single RESP3 encode; this references it via a zero-copy buffer
  // fragment (no per-subscriber payload copy) whose releasor keeps ``bytes`` alive until the
  // connection — or the FIFO park buffer — has drained it. Same out-of-band vs FIFO routing as
  // deliverMessage: through the ordering sink when wired, else a direct connection write.
  void deliverSharedFrame(const std::shared_ptr<const std::string>& bytes);

  // Wire the per-connection ProxyFilter's push-ordering sink (held weakly — see PushOrderingSink).
  void setOrderingSink(const std::shared_ptr<PushOrderingSink>& sink) { ordering_sink_ = sink; }

  // Tear down this subscriber by closing its downstream connection (NoFlush — buffered data
  // has nowhere to land once the upstream cluster is gone). LocalClose fires synchronously,
  // so callers driving close() in a teardown loop must finish all registry-side state mutations
  // the resulting ProxyFilter::onEvent will read (notably emptying the per-registry subscriber
  // maps so the onEvent UNSUBSCRIBE walk no-ops for this registry) BEFORE entering the close loop;
  // the active-subscriptions gauge itself is drained by ~DownstreamSubscriber (A-2), not here. Used
  // by SubscriptionRegistry::clear() on cluster removal/update so a subscribed downstream client
  // does not silently stay "subscribed" against a registry the proxy no longer owns.
  void close();

  const absl::flat_hash_set<std::string>& subscribedChannels() const {
    return subscribed_channels_;
  }

  uint64_t totalSubscriptionCount() const { return subscribed_channels_.size(); }

  // Membership mutation is owned by the subscriber (A-6): the registry adds/removes a channel
  // through these rather than reaching into ``subscribed_channels_`` directly, and the
  // ``pubsub_active_subscriptions`` gauge moves in lockstep. The gauge therefore equals the sum of
  // every live subscriber's channel count BY CONSTRUCTION (A-2) — one inc/dec site per set mutation
  // plus the destructor drain below, replacing the former three-way accounting (splitter delta /
  // filter-disconnect subtract / ack-timeout rollback). Each returns whether the set actually
  // changed, so the registry can still detect a channel's first/last subscriber on this thread.
  bool addChannel(const std::string& channel) {
    const bool inserted = subscribed_channels_.insert(channel).second;
    if (inserted) {
      stats_.active_subscriptions.inc();
    }
    return inserted;
  }
  bool removeChannel(const std::string& channel) {
    const bool erased = subscribed_channels_.erase(channel) > 0;
    if (erased) {
      stats_.active_subscriptions.dec();
    }
    return erased;
  }

  // Record the ASYNC outcome of a pub/sub SUBSCRIBE when its upstream ack finally resolves: success
  // when the upstream SSUBSCRIBE is acked (deliverPendingSubscribeAck), error when it errors
  // upstream or the ack times out (failPendingSubscribers). Distinct from the ``command.subscribe``
  // stat, which counts "upstream send accepted" at send time — these count real subscribe outcomes.
  void recordSubscribeAckSuccess() { stats_.subscribe_ack_success.inc(); }
  void recordSubscribeAckError() { stats_.subscribe_ack_error.inc(); }

private:
  // Deliver whatever a deliverMessage / deliverSharedFrame call has just staged in
  // ``encode_buffer_`` to the wire in FIFO order, then leave the buffer empty. Through the ordering
  // sink when one is wired (production — the owning filter places the push after any in-flight
  // command replies and counts it at ACTUAL delivery, so we do NOT count here — D3), else a direct
  // connection write that drains the buffer and counts the push now (registry-only tests, or the
  // filter is gone). Shared tail of both message-delivery paths.
  void routePush();

  // No ``friend class SubscriptionRegistry`` (D4): the registry reaches this subscriber ONLY
  // through the public surface — subscribedChannels() for the read walk,
  // addChannel()/removeChannel() for membership. Keeping ``subscribed_channels_`` private is what
  // type-enforces the A-2 gauge invariant ("the pubsub_active_subscriptions gauge moves only via
  // addChannel/removeChannel"): a future registry edit cannot silently mutate the set past the
  // gauge and desync it — exactly the bug class round 3 set out to make unrepresentable.
  absl::flat_hash_set<std::string> subscribed_channels_;
  Network::Connection& connection_;
  Common::Redis::EncoderImpl encoder_;
  Buffer::OwnedImpl encode_buffer_;
  const DownstreamSubscriberStats stats_;
  // Weak ref to the owning ProxyFilter's push-ordering sink (see PushOrderingSink). Empty in
  // registry unit tests that run without a filter, and once the filter is destroyed; deliverMessage
  // then falls back to a direct connection write.
  std::weak_ptr<PushOrderingSink> ordering_sink_;
};

using DownstreamSubscriberPtr = std::shared_ptr<DownstreamSubscriber>;

/**
 * Build a RESP3 subscribe/unsubscribe ack Push frame ``[verb, target, count]``. Shared by the
 * splitter (immediate/dedup and bare-unsubscribe acks) and the registry
 * (deliverPendingSubscribeAck), so the ack shape can only be changed in one place.
 * @param command_name the downstream-facing verb (``subscribe`` / ``unsubscribe``).
 * @param target the channel name, or nullptr for a bare ``UNSUBSCRIBE`` with no channels (emitted
 *        as a RESP3 Null).
 * @param subscription_count the subscriber's per-subscriber count carried in the ack.
 */
Common::Redis::RespValue makeSubscriptionAck(const std::string& command_name,
                                             const std::string* target, int64_t subscription_count);

/**
 * Interface for sending subscribe/unsubscribe commands upstream.
 * Implemented by the connection pool (ThreadLocalPool).
 */
class UpstreamSubscriptionCallbacks {
public:
  virtual ~UpstreamSubscriptionCallbacks() = default;

  // Outcome of a fire-and-forget upstream ``SUNSUBSCRIBE``. Distinguishes a connection that stays
  // open (its ack will arrive) from one the conn pool retired INLINE while sending — a host's LAST
  // channel SUNSUBSCRIBE triggers maybeCleanupSubscriptionMode, which closes the now-idle
  // subscription connection, so no ack will ever come. The registry uses this to avoid recording
  // (or leaving) a stale expected ack that a later genuine unsolicited SUNSUBSCRIBE would be
  // swallowed by.
  enum class SunsubscribeResult {
    NotSent,           // no host / no subscription client — nothing was sent
    AckExpected,       // sent; the connection stays open and will deliver the ack
    ConnectionRetired, // sent on the host's last channel — the conn pool retired the connection
                       // inline, so no ack will arrive
  };

  /**
   * Resolve the upstream host that currently owns ``channel``'s hash slot WITHOUT sending anything
   * or creating a subscription connection. Lets ``onClusterTopologyChange`` skip channels whose
   * owner is unchanged instead of churning every subscription on a slot-only rebalance.
   * @param channel the channel whose slot owner to resolve (hash-slot routing).
   * @return the current slot owner, or nullptr if none is available (no cluster / all hosts down).
   */
  virtual Upstream::HostConstSharedPtr
  chooseUpstreamHostForChannel(const std::string& channel) PURE;

  /**
   * SHARD_MEMBERS placement candidates for ``channel``: the members (primary + replicas) of the
   * shard that owns the channel's hash slot, appended to ``out``. HEALTHY members first, and when
   * at least one member is healthy only the healthy ones are offered — health filters a NEW
   * placement (D2), it never re-homes an existing one. The registry picks the least-loaded of
   * these.
   * @param channel the channel whose slot shard's members to enumerate.
   * @param out receives the candidate hosts (only appended to; existing entries are left intact).
   * @return false when the upstream has no shard-membership model (a non-cluster load balancer) or
   * the slot has no shard snapshot yet (transient). The registry then degrades this channel's
   * placement to ``chooseUpstreamHostForChannel`` (the slot primary).
   */
  virtual bool shardCandidatesForChannel(const std::string& channel,
                                         std::vector<Upstream::HostConstSharedPtr>& out) PURE;

  /**
   * Whether ``host`` still serves ``channel``'s hash slot — the SHARD_MEMBERS record-validity
   * check.
   * ``as_primary`` true asks "is ``host`` the slot's current PRIMARY?"; false asks "is ``host``
   * still a MEMBER (primary or replica) of the slot's shard?". The membership answer is
   * health-AGNOSTIC, so a momentarily-unhealthy member keeps its subscriptions rather than churning
   * them on a flap (D2).
   * @return true when ``host`` serves the slot as asked, AND true when the slot has no shard
   * snapshot yet (transient — keep the record, matching the PRIMARY policy's null-resolution
   * tolerance, G4). False means the record is stale and the channel must be re-placed.
   */
  virtual bool hostServesChannelSlot(const std::string& channel,
                                     const Upstream::HostConstSharedPtr& host,
                                     bool as_primary) PURE;

  /**
   * Send ``SSUBSCRIBE`` for a single channel to an ALREADY-resolved host. The registry resolves the
   * placement target itself (chooseUpstreamHostForChannel for the PRIMARY policy — §7 P1) and hands
   * it here, so a fresh subscribe, a connection-loss reissue, and a topology reroute all send
   * through this one primitive with no internal slot resolve. Unlike SUBSCRIBE (any node),
   * ``SSUBSCRIBE`` must reach the shard that owns the channel's hash slot — the caller's ``host``
   * is that shard.
   * @param channel the channel to subscribe to.
   * @param push_callbacks the push message handler.
   * @param host the pre-resolved upstream host to send to (the channel's placement target).
   * @return true if sent successfully.
   */
  virtual bool
  sendUpstreamSsubscribeToHost(const std::string& channel,
                               Common::Redis::Client::PushMessageCallbacks& push_callbacks,
                               const Upstream::HostConstSharedPtr& host) PURE;

  /**
   * Send ``SUNSUBSCRIBE`` for a single channel to the specified host.
   * @param channel the channel to unsubscribe.
   * @param host the upstream host to send ``SUNSUBSCRIBE`` to (from the channel→host map).
   * @return whether the send happened and, if so, whether the connection stayed open (AckExpected)
   * or was retired inline as the host's last channel (ConnectionRetired). See SunsubscribeResult.
   */
  virtual SunsubscribeResult sendUpstreamSunsubscribe(const std::string& channel,
                                                      Upstream::HostConstSharedPtr host) PURE;

  /**
   * Schedule a deferred re-subscribe with backoff. Called by the registry when an upstream
   * (re-)subscribe fails (e.g., Redis is down). The implementation drives the registry's own
   * ``doResubscribe()`` when the timer fires — the registry is the single source of truth for
   * what to re-issue, so no callback is threaded through here.
   * @param delay milliseconds to wait before retrying.
   */
  virtual void scheduleResubscribe(std::chrono::milliseconds delay) PURE;

  /**
   * @return whether a re-subscribe cycle is already scheduled — the timer armed by a prior
   * ``scheduleResubscribe`` has not yet fired ``doResubscribe``. The registry uses this to COALESCE
   * a burst of re-subscribe signals (B-1): each signal enrolls its channel(s) in the explicit retry
   * scope, but only the first arms the backoff timer; the rest ride the already-scheduled cycle
   * instead of advancing the backoff per-signal or resetting the pending deadline.
   */
  virtual bool resubscribeTimerPending() const PURE;

  /**
   * Request an upstream cluster topology (slot-map) refresh. Called when a subscription-connection
   * control reply carries a ``-MOVED`` / ``-ASK`` / ``-CLUSTERDOWN`` — the local slot map is stale,
   * so a plain host-scoped re-subscribe would just re-resolve to the same wrong owner. The conn
   * pool routes this through the shared cluster refresh manager (which throttles), matching the
   * data path's redirection handling.
   */
  virtual void requestTopologyRefresh() PURE;

  /**
   * Retire ``host``'s dedicated subscription connection if it no longer carries any subscription
   * (Issue 5). Used after an UNSOLICITED SUNSUBSCRIBE (slot moved off ``host``) forgets the last
   * channel mapping for that host: unlike the client-driven UNSUBSCRIBE path, no SUNSUBSCRIBE is
   * sent — so the connection would otherwise linger idle until an unrelated event closes it. The
   * conn pool checks the registry's per-host index (hostHasSubscriptions) and retires the now-idle
   * client.
   * @return whether the connection was actually retired — false when ``host`` is null (registry
   * test injection) or still has channels. The caller uses this REPORTED decision (not its own
   * prediction of it) to drop the host's control ledger when true: a retired connection will never
   * ack anything queued on its FIFO, and a stale head would black out a future subscribe routed
   * back to the host. This mirrors ``sendUpstreamSunsubscribe``'s ``ConnectionRetired`` result, so
   * every retire path clears the ledger off the pool's actual decision rather than four sites
   * re-deriving it (ALT-1).
   */
  virtual bool retireSubscriptionConnectionIfIdle(const Upstream::HostConstSharedPtr& host) PURE;
};

/**
 * Per-thread subscription registry. Lives inside ThreadLocalPool.
 * Implements PushMessageCallbacks to receive Push messages from upstream.
 *
 * When the first downstream subscriber subscribes to a channel, the registry
 * sends ``SSUBSCRIBE`` upstream via UpstreamSubscriptionCallbacks, routed to the
 * shard that owns the channel's hash slot. When the last subscriber unsubscribes,
 * it sends ``SUNSUBSCRIBE`` upstream. Cluster sharding is transparent to the client:
 * the splitter rewrites client ``SUBSCRIBE``/``UNSUBSCRIBE`` into the sharded verbs
 * and the downstream acks always read ``subscribe``/``unsubscribe``.
 *
 * When an upstream connection is lost, onUpstreamConnectionClose() is called
 * to re-subscribe all affected channels on a new connection.
 */
class SubscriptionRegistry : public Common::Redis::Client::PushMessageCallbacks,
                             public Logger::Loggable<Logger::Id::redis> {
public:
  // ``subscribe_ack_timeout`` / ``resubscribe_backoff_base`` / ``resubscribe_backoff_max`` are the
  // pub/sub tuning knobs (A-7), threaded from ConnPoolSettings.pubsub_settings by the conn pool.
  // ``placement`` is the channel homing policy (§7 P3): the conn pool passes the EFFECTIVE value
  // (SHARD_MEMBERS already degraded to Primary for a non-cluster upstream). All default to the
  // historical values so unit tests (and any caller that omits them) keep the original behavior.
  SubscriptionRegistry(UpstreamSubscriptionCallbacks& upstream_callbacks,
                       Random::RandomGenerator& random, Event::Dispatcher& dispatcher,
                       std::chrono::milliseconds subscribe_ack_timeout =
                           std::chrono::milliseconds(kSubscribeAckTimeoutMs),
                       std::chrono::milliseconds resubscribe_backoff_base =
                           std::chrono::milliseconds(kInitialResubscribeBackoffMs),
                       std::chrono::milliseconds resubscribe_backoff_max =
                           std::chrono::milliseconds(kMaxResubscribeBackoffMs),
                       Common::Redis::Client::SubscriptionPlacement placement =
                           Common::Redis::Client::SubscriptionPlacement::Primary);
  ~SubscriptionRegistry() override = default;

  /**
   * Result of a subscribe call. ``success`` is false when the upstream send fails (no
   * healthy host, conn-pool failure, client-side hold/dispatch failure) and the registry
   * has rolled back the local subscription state — the splitter must surface this as an
   * inline error to the downstream client and not record a fake success ack.
   * ``subscription_count`` is the *subscriber's* total channel+shard count after
   * the operation (matching the count Redis returns in a real subscribe ack).
   * ``ack_deferred`` is true when the registry registered a pending ack for this subscriber and
   * will deliver the downstream subscribe ack when the upstream ack arrives
   * (deliverPendingSubscribeAck) — either because it issued a fresh upstream subscribe, or because
   * it joined an existing channel that is still awaiting its first upstream ack. When false, the
   * channel was already ACTIVE (a prior upstream ack has landed and drained the pending bucket) so
   * no upstream ack will fire for this subscriber and the splitter fabricates the ack immediately.
   * Deferring while the channel is still subscribing is what prevents a second subscriber from
   * seeing a premature success the upstream might still reject.
   */
  struct SubscribeResult {
    bool success;
    uint64_t subscription_count;
    bool ack_deferred;
  };

  /**
   * Per-shard subscribe (``SSUBSCRIBE``, Redis 7.0+) of a SINGLE channel, routed to the shard that
   * owns its hash slot. This is the sole production subscription path: the splitter rewrites every
   * client ``SUBSCRIBE`` into this transparently (one channel per call), so the downstream client
   * always sees ``["subscribe", channel, count]`` (the internal ``ssubscribe`` verb is never
   * surfaced). Single-channel by design (S-2): the former Span form carried batch step machinery
   * whose fresh+dedup skew (G13) only a never-used multi-channel call could hit.
   */
  SubscribeResult subscribe(const std::string& channel, const DownstreamSubscriberPtr& subscriber);

  /**
   * Per-shard unsubscribe (``SUNSUBSCRIBE``).
   *
   * When ``preserved_acks`` is non-null and a channel's SUBSCRIBE is still awaiting its upstream
   * ack (Redis-compatible SUBSCRIBE-then-UNSUBSCRIBE, Issue 4), the ``subscribe`` ack that would
   * otherwise be dropped is APPENDED to ``preserved_acks`` instead of delivered here — the caller
   * (splitter) flushes them, ahead of its own ``unsubscribe`` ack, only AFTER its terminal
   * ``respond()`` so a synchronous downstream close cannot re-enter this registry mid-teardown
   * (the mid-loop deliver was a reentrancy/UAF hazard). A null ``preserved_acks`` (the F1 rollback
   * in failPendingSubscribers) simply drops the pending entry.
   */
  uint64_t unsubscribe(absl::Span<const std::string> channels,
                       const DownstreamSubscriberPtr& subscriber,
                       std::vector<Common::Redis::RespValue>* preserved_acks = nullptr);

  /**
   * Remove all subscriptions for a disconnecting downstream subscriber.
   */
  void removeSubscriber(const DownstreamSubscriberPtr& subscriber);

  /**
   * Called when an upstream connection carrying subscriptions is lost. With a specific ``host``,
   * only that host's channels are re-issued; with a null host (topology change / re-subscribe retry
   * fallback), every active channel is re-subscribed. Each re-issue routes to its correct shard.
   */
  void onUpstreamConnectionClose(const Upstream::HostConstSharedPtr& host = nullptr);

  /**
   * Forget the control-ledger state (expected SUNSUBSCRIBE acks + outstanding control commands) of
   * a host whose subscription connection just closed. Split out of onUpstreamConnectionClose so the
   * conn-pool can run it SYNCHRONOUSLY at close time (G7): a deferred clear could otherwise wipe
   * the fresh ledger of a replacement subscription connection created to the same host within the
   * same dispatcher iteration. A null host is a no-op (the resubscribe-all fallback carries no
   * per-host ledger to clear).
   */
  void forgetHostConnectionLedger(const Upstream::HostConstSharedPtr& host);

  /**
   * Seed the re-subscribe retry scope (pending_resubscribe_channels_) with a closed host's
   * channels, KEEPING their channel→host owner (D1). Exposed so the conn-pool can run it
   * SYNCHRONOUSLY at close time (S6-5): a DEFERRED seed could capture a replacement subscription
   * connection's freshly-subscribed channel — one created to the same host within the same
   * dispatcher iteration — and spuriously re-SSUBSCRIBE it. Pair with a deferred
   * armResubscribeBackoff(). A null host seeds every active channel (whole-registry fallback).
   */
  void markHostChannelsForResubscribe(const Upstream::HostConstSharedPtr& host);
  /**
   * Arm the (pool-touching) backoff that drives the next doResubscribe(). Exposed so the conn-pool
   * can DEFER just this half of a connection-close via dispatcher post while seeding the retry
   * scope synchronously (S6-5 / G7). Safe against pool teardown: no-ops once clear() has emptied
   * subscriptions_, before reaching upstream_callbacks_.
   */
  void armResubscribeBackoff() { scheduleResubscribe(); }

  /**
   * Called when cluster topology changes (scale out/in).
   * Re-routes per-shard ``SSUBSCRIBE`` channels whose hash slots may have moved
   * to different shards.
   */
  void onClusterTopologyChange();

  // PushMessageCallbacks
  // ``host`` is the source subscription connection's host — mandatory (S-4): production always
  // passes it (client_impl) and unit tests inject a mock, so the stale-ack defenses that key on it
  // (ssubscribeAckIsCurrent, the unsolicited-SUNSUBSCRIBE owner check) are never silently bypassed
  // by a null. It correlates control-command acks to the per-host outstanding-command FIFO (see
  // onUpstreamControlError).
  void onPushMessage(Common::Redis::RespValuePtr&& value,
                     const Upstream::HostConstSharedPtr& host) override;
  void onUpstreamControlError(Common::Redis::RespValuePtr&& value,
                              const Upstream::HostConstSharedPtr& host) override;

  // Registry-wide distinct-channel total across ALL downstream subscribers on this thread.
  // Test-only assertion on registry state — production subscription-delta detection uses the
  // per-subscriber DownstreamSubscriber::totalSubscriptionCount() (see the subscribe/unsubscribe
  // paths in command_splitter_impl.cc), never a registry-wide sum.
  uint64_t subscriptionCount() const { return subscriptions_.size(); }
  bool empty() const { return subscriptions_.empty(); }

  void clear();

  // Re-issue every active (S)SUBSCRIBE on a fresh upstream connection. Driven by the
  // conn pool's resubscribe timer (see scheduleResubscribe) after a connection loss; public so
  // the pool can call it directly rather than holding a stored callback + strong-ref pair.
  void doResubscribe();

  // Forget the channel→host mapping for a host the cluster just removed. Called by the conn pool
  // from onHostsRemoved BEFORE any topology-change re-route runs, so the registry never issues a
  // ``SUNSUBSCRIBE`` to the decommissioned host (which would auto-recreate a client and open a
  // connection to a dead endpoint). The affected channels stay in ``subscriptions_`` and are
  // re-subscribed to their new shard owner by the resubscribe/topology-change path.
  void dropHost(const Upstream::HostConstSharedPtr& host);

  // The current SSUBSCRIBE attempt for a channel: the host we last sent it to, and a monotonic
  // generation that uniquely identifies THAT send. The generation is what disambiguates a channel's
  // successive attempts even when they target the SAME host (an A -> B -> A re-route leaves the
  // host equal but the generation different), so an upstream ack — correlated back to its attempt
  // via the per-host control-command FIFO — only completes/advances the channel when BOTH host and
  // generation match. See ssubscribeAckIsCurrent / recordSsubscribeAttempt.
  struct ChannelOwner {
    Upstream::HostConstSharedPtr host;
    uint64_t generation;
  };

  const absl::flat_hash_map<std::string, ChannelOwner>& channelHosts() const {
    return channel_hosts_;
  }

  // O(1) "does this host still serve any subscription?" check for the conn pool's
  // maybeCleanupSubscriptionMode, backed by ``host_channels_`` (maintained alongside
  // ``channel_hosts_``). Replaces a full linear scan of ``channel_hosts_`` per SUNSUBSCRIBE, which
  // made mass unsubscribe O(n^2) in the number of channels on the host.
  bool hostHasSubscriptions(const Upstream::HostConstSharedPtr& host) const {
    // Empty sets are never kept (removeHostChannel drops the host on its last channel), so mere
    // presence in the index means the host still serves at least one channel.
    return host_channels_.contains(host);
  }

private:
  // Channel -> its set of subscribers on THIS registry. A channel present in the map always has a
  // non-empty subscriber set (the last removal erases the entry).
  using SubsMap = absl::flat_hash_map<std::string, absl::flat_hash_set<DownstreamSubscriberPtr>>;

  // Helpers for subscribe/unsubscribe deduplication. Both operate on this registry's
  // ``subscriptions_`` and mirror the affected channel(s) into the subscriber's cross-registry
  // ``subscribed_channels_`` set; the caller-supplied map/set were always those two, so they are no
  // longer parameters (S-7). addSubscription takes a single channel (subscribe() is always
  // per-channel — the multi-channel span helper was S-2 residue) and returns whether this registry
  // newly owns it (0->1 subscribers, i.e. needing an upstream SSUBSCRIBE); removeSubscriptions
  // takes a span (bare UNSUBSCRIBE drops N channels at once) and returns those orphaned here (last
  // subscriber gone, needing an upstream SUNSUBSCRIBE).
  bool addSubscription(const std::string& channel, const DownstreamSubscriberPtr& subscriber);
  std::vector<std::string> removeSubscriptions(absl::Span<const std::string> keys,
                                               const DownstreamSubscriberPtr& subscriber);

  // Fan a single pub/sub ``message`` frame out to every subscriber of a channel. Snapshots the
  // subscriber set first (deliver() can re-enter removeSubscriber and rehash the set mid-fan-out)
  // and, when more than one subscriber is present, RESP-encodes the frame once and reuses those
  // bytes for all of them rather than re-encoding per subscriber. Shared by the ``message`` and
  // ``smessage`` branches of onPushMessage.
  void deliverFrameToSubscribers(const absl::flat_hash_set<DownstreamSubscriberPtr>& subscribers,
                                 const Common::Redis::RespValue& frame);

  // Per-channel pending subscribe-ack state. When ``ssubscribe`` issues an upstream send, the
  // calling subscriber is parked here keyed by the (target, command) tuple. When the matching
  // upstream subscribe-ack Push arrives via
  // onPushMessage, the registry drains the entry and delivers a per-subscriber ack with
  // the subscriber's current count. A subscriber that hits the dedup path is parked here too when
  // the channel is still subscribing (its bucket is still open): it joins that bucket and is acked
  // when the shared upstream ack lands (F1), so no premature success is fabricated ahead of the
  // upstream. Only when the channel is already active (its bucket already drained) is the dedup
  // subscriber left unparked and acked immediately by the splitter, since no further upstream ack
  // will arrive on its behalf.
  //
  // Cleanup paths:
  //   * Successful ack delivery: drain consumed in deliverPendingSubscribeAck.
  //   * Subscriber disconnect / unsubscribe before ack: removeSubscriber and the
  //     unsubscribe-family methods sweep the map and remove dropped subscribers.
  //   * Upstream connection close before ack: the entry stays; the resubscribe path will
  //     re-issue the upstream subscribe and the new ack will drain the entry.
  struct PendingSubscribeAck {
    std::weak_ptr<DownstreamSubscriber> subscriber;
    // Subscriber's per-subscriber count captured AT subscribe-call time, not ack-arrival
    // time. Multi-channel SUBSCRIBE issues N upstream subscribes back-to-back so the
    // subscriber's count grows during the loop (1, 2, ...); when the upstream acks arrive
    // later, all of them would otherwise see the final count and the per-channel ack would
    // lose its step number. The snapshot preserves Redis's "number of subscriptions the
    // client now has at THIS step" semantics that the previous immediate-fabrication path
    // delivered.
    uint64_t snapshot_count;
    // Whether THIS registration newly established the subscriber's subscription to the channel
    // (false for a DUPLICATE SUBSCRIBE from a subscriber that already held it). On an upstream
    // failure, failPendingSubscribers rolls back ONLY newly_added entries, never a duplicate that
    // merely echoes a still-live subscription (#1).
    bool newly_added;
  };
  // All subscribers waiting on one channel's upstream subscribe share ONE pending bucket: they are
  // all waiting on the same upstream ack, so a fan-in of N subscribers is failed in one O(N) drain
  // on timeout (not N per-subscriber timers each doing an O(N) removal, i.e. O(N^2)). The timeout
  // itself is NOT a per-bucket timer either — every bucket shares the registry's SINGLE
  // subscribe-ack timer, scheduled via ``subscribe_ack_schedule_`` (E4), since the deadline is the
  // constant kSubscribeAckTimeoutMs and never varies.
  struct PendingBucket {
    std::vector<PendingSubscribeAck> entries_;
    // Identity token for this bucket's live schedule entry in ``subscribe_ack_schedule_`` (E4).
    // Assigned when the bucket is created; a channel that is acked (bucket erased) then
    // re-subscribed gets a FRESH token, so when the single timer fires and finds the old schedule
    // entry it can tell the entry belongs to a superseded registration (token mismatch) and skip it
    // — the fresh registration has its own later schedule entry.
    uint64_t schedule_seq_{0};
  };
  // Keyed by target channel. The upstream verb is ALWAYS ``ssubscribe`` (every subscription is
  // issued as SSUBSCRIBE), so it carries no information and is not part of the key (C2/E8). The
  // downstream ack always emits the client verb ``subscribe`` — cluster sharding is a transparent
  // wire detail the client never sees.
  absl::flat_hash_map<std::string, PendingBucket> pending_subscribe_acks_;

  // The single shared subscribe-ack timeout timer + its deadline queue (E4) are owned by the
  // ``ack_scheduler_`` member (SubscribeAckDeadlineScheduler — extracted, D3). A pending bucket's
  // ``schedule_seq_`` is the token ack_scheduler_.schedule() returns, matched by its live-predicate
  // on fire so a stale entry (bucket acked/dropped, or re-created under a new token) is skipped.

  // Arm the backoff to drive the next doResubscribe(), which re-issues every channel in the
  // explicit retry scope pending_resubscribe_channels_ (D1). Callers that have already seeded
  // exactly the channel(s) they need re-resolved (an unsolicited SUNSUBSCRIBE that added its one
  // migrated channel, or a re-issued-SSUBSCRIBE error) call this directly; whole-host callers go
  // through scheduleResubscribeForHost, which seeds that host's channels first.
  void scheduleResubscribe();
  // Seed ``host``'s channels into the retry scope (markHostChannelsForResubscribe, KEEPING their
  // owner — D1) then arm the backoff. Shared by onUpstreamConnectionClose (connection dropped) and
  // the whole-host onUpstreamControlError paths (error reply, connection kept open).
  void scheduleResubscribeForHost(const Upstream::HostConstSharedPtr& host);
  void registerPendingSubscribeAck(const std::string& target,
                                   const DownstreamSubscriberPtr& subscriber,
                                   uint64_t snapshot_count, bool newly_added);
  // Append an entry to an ALREADY-RESOLVED pending-ack bucket (arming the shared ack timeout on the
  // empty -> non-empty edge). Lets subscribe()'s dedup-join reuse a bucket it found with one lookup
  // instead of re-hashing the channel through registerPendingSubscribeAck's ``operator[]``.
  void appendPendingSubscribeAck(PendingBucket& bucket, const std::string& target,
                                 const DownstreamSubscriberPtr& subscriber, uint64_t snapshot_count,
                                 bool newly_added);
  // Deliver the parked downstream ``subscribe`` ack(s) for ``target`` when its upstream ack lands.
  // ``ack_is_current`` (computed by the caller via ssubscribeAckIsCurrent) gates delivery: a stale
  // ack — owner-less gap, wrong host, or a superseded same-host attempt — leaves the bucket parked
  // for the current attempt's ack instead of handing the client a premature success. Mandatory
  // (§6): the former ``= true`` default lost its rationale when S-4 made the ack's source host
  // mandatory, and the sole caller (onPushMessage) always passes the computed value — a defaulted
  // "current" is a dangerous silent-success footgun for any future caller.
  void deliverPendingSubscribeAck(const std::string& target, bool ack_is_current);
  // Fail a channel whose upstream subscribe was never acked (F4) — the ack_scheduler_'s expiry
  // callback. ``target`` is by value: it names a bucket this call erases (failPendingSubscribers),
  // and the scheduler holds it in a deadline entry that may be destroyed as the queue is drained,
  // so a reference could dangle. Rolls back every pending subscriber on the channel and CLOSES its
  // connection (F3; see failPendingSubscribers) so the client reconnects and retries cleanly.
  void handleSubscribeAckTimeout(std::string target);
  // Retry the current re-subscribe generation when not all its re-issued SSUBSCRIBEs acked within
  // the timeout window (Issue 3). A re-issued SSUBSCRIBE for an already-active channel has no
  // downstream ack bucket / per-bucket timer, so a silently-lost ack would otherwise stall it
  // forever; this re-resolves the unacked channels on the escalating backoff.
  void handleResubscribeGenerationTimeout();
  // --- Subscription PLACEMENT seam (§7 P1) ---
  // A channel's home is the RECORD (``channel_hosts_[channel]``); placement policy is consulted
  // only when a channel is first placed or must be re-placed, and stability comes from the record,
  // not the policy. These two helpers are the only seams a non-PRIMARY placement policy (P3)
  // touches.
  //
  // Is the recorded owner still valid to keep serving this channel?
  //   PRIMARY: validity is "matches the current slot-owner resolution, OR that resolution is
  //     transiently null (resharding-window / momentary-master-absence blip — G4)". The resolution
  //     is returned via ``resolved_out`` so a subsequent re-placement can reuse it (E-8).
  //   SHARD_MEMBERS: validity is shard MEMBERSHIP — is ``recorded`` still a member of the slot's
  //     shard? Health-AGNOSTIC (D2), so a momentarily-unhealthy member keeps its channels rather
  //     than churning on a flap. A membership query is NOT a placement resolution, so leave
  //     ``resolved_out`` null — an invalid record re-places through resolvePlacement (which
  //     resolves once). hostServesChannelSlot reports true on a transient no-shard-snapshot, giving
  //     the same keep-the-record tolerance PRIMARY has for a null resolution (G4).
  //
  // ``health_aware`` distinguishes the two callers. A topology event (onClusterTopologyChange)
  // passes false: membership is health-agnostic there so a momentarily-unhealthy member is not
  // re-homed on a flap (D2). A FAILURE-driven re-issue (reissueSsubscribe from a lost connection /
  // failed send) passes true: an owner that is still a shard member but UNHEALTHY has no escape
  // under pure membership validity — it would be retried forever while healthy siblings sit idle —
  // so treat it as invalid and re-place onto a healthy member. A real retry is not a flap. (No
  // effect under PRIMARY, whose only candidate is the slot primary regardless of health.)
  bool recordedOwnerValid(const std::string& channel, const Upstream::HostConstSharedPtr& recorded,
                          Upstream::HostConstSharedPtr& resolved_out, bool health_aware = false) {
    if (placement_ == Common::Redis::Client::SubscriptionPlacement::ShardMembers) {
      resolved_out = nullptr;
      if (!upstream_callbacks_.hostServesChannelSlot(channel, recorded, /*as_primary=*/false)) {
        return false; // left the shard -> re-place
      }
      if (health_aware && recorded != nullptr &&
          recorded->coarseHealth() != Upstream::Host::Health::Healthy) {
        return false; // persistent-failure escape -> re-place onto a healthy member
      }
      return true;
    }
    resolved_out = upstream_callbacks_.chooseUpstreamHostForChannel(channel);
    return resolved_out == nullptr || resolved_out == recorded;
  }
  // Resolve a placement target for a channel that has no valid record (fresh subscribe, or a record
  // that just went invalid). PRIMARY: the slot-owner resolve. SHARD_MEMBERS: the least-loaded of
  // the slot shard's candidate members, degrading to the slot primary when the upstream has no
  // membership model (non-cluster — normally caught at construction) or the slot has no shard yet
  // (transient) — D3.
  Upstream::HostConstSharedPtr resolvePlacement(const std::string& channel) {
    if (placement_ == Common::Redis::Client::SubscriptionPlacement::ShardMembers) {
      std::vector<Upstream::HostConstSharedPtr> candidates;
      if (upstream_callbacks_.shardCandidatesForChannel(channel, candidates) &&
          !candidates.empty()) {
        return leastLoadedOf(candidates);
      }
      return upstream_callbacks_.chooseUpstreamHostForChannel(channel); // degrade (D3)
    }
    return upstream_callbacks_.chooseUpstreamHostForChannel(channel);
  }
  // SHARD_MEMBERS tie-break: pick the candidate carrying the FEWEST channels in THIS registry
  // (absent = 0), uniformly random among ties (D4). Subscription placement is a durable state
  // registration, not a per-request pick, so balancing at placement time accumulates; a freshly
  // added replica starts at 0 and wins subsequent placements until it catches up — passive
  // rebalance without moving any live subscription (D1/D7). ``candidates`` must be non-empty.
  Upstream::HostConstSharedPtr
  leastLoadedOf(const std::vector<Upstream::HostConstSharedPtr>& candidates) {
    std::vector<Upstream::HostConstSharedPtr> best;
    size_t best_count = 0;
    for (const auto& host : candidates) {
      auto it = host_channels_.find(host);
      const size_t count = (it == host_channels_.end()) ? 0 : it->second.size();
      if (best.empty() || count < best_count) {
        best_count = count;
        best.assign(1, host);
      } else if (count == best_count) {
        best.push_back(host);
      }
    }
    return best.size() == 1 ? best.front() : best[random_.random() % best.size()];
  }

  // Re-issue an SSUBSCRIBE for an already-active channel and enroll it in the CURRENT resubscribe
  // generation: seed ``pending_resubscribe_channels_`` (so a lost ack keeps the generation
  // incomplete) and, on success, record the target host as the channel's expected ack owner and its
  // outstanding control command. Placement (§7 P1): if the RECORD is still valid, re-target the
  // recorded host (a retry is not a move); only a channel with no valid record is re-placed. Shared
  // by the connection-loss (doResubscribe) and slot-move (onClusterTopologyChange) reissue paths so
  // BOTH register the generation identically (Issue 4). Returns whether the upstream send
  // succeeded.
  bool reissueSsubscribe(const std::string& channel,
                         const Upstream::HostConstSharedPtr& pre_resolved_host = nullptr);
  // Arm the generation ack timeout for the channels just enrolled via reissueSsubscribe, if any and
  // if a dispatcher is present (production). Shared by both reissue paths (Issue 3/4) so a
  // silently lost ack is retried whether the reissue came from a dropped connection or a slot
  // migration.
  void armResubscribeGenerationTimer();
  // Fail every pending subscriber on ``target``: roll back its optimistic subscription (undoing the
  // gauge increment) and CLOSE its connection (F3) rather than writing a bare out-of-band ``-ERR``,
  // which a pipelining RESP3 client would misattribute to an earlier in-flight command and desync.
  // ``error_message`` is logged (not sent on the wire). Shared by the subscribe-ack timeout and the
  // immediate upstream-SSUBSCRIBE-error path (onUpstreamControlError).
  bool failPendingSubscribers(const std::string& target, const std::string& error_message);
  void dropPendingForSubscriber(const DownstreamSubscriberPtr& subscriber);
  // Remove ``subscriber``'s entries (and any expired-weak ones) from the pending-ack bucket for
  // ``target``, erasing the bucket if it empties; other subscribers on the channel stay pending.
  // The sole per-(channel, subscriber) pending scrub (R-2): used by the unsubscribe /
  // collect-preserved paths and, over every bucket, by dropPendingForSubscriber.
  void scrubPendingBucket(const std::string& target, const DownstreamSubscriberPtr& subscriber);
  // Redis-compatible SUBSCRIBE-then-UNSUBSCRIBE (Issue 4): if ``subscriber`` still has a pending
  // subscribe ack parked for ``target`` (its upstream SSUBSCRIBE ack has not landed yet), APPEND
  // that
  // ``subscribe`` ack to ``out_acks`` and drop the entry — instead of silently discarding it — so a
  // client that pipelines ``SUBSCRIBE ch`` / ``UNSUBSCRIBE ch`` still sees ``subscribe ch`` before
  // the ``unsubscribe ch`` ack. Crucially this does NOT write downstream: the caller flushes
  // ``out_acks`` only after its terminal ``respond()`` (reentrancy-safe — see unsubscribe). Only
  // this subscriber's entries are drained; other pending subscribers on the same channel keep
  // waiting. Returns whether any ack was collected.
  bool collectPreservedSubscribeAcks(const std::string& target,
                                     const DownstreamSubscriberPtr& subscriber,
                                     std::vector<Common::Redis::RespValue>& out_acks);

  // ``host_channels_`` reverse-index bookkeeping, kept in lockstep with every ``channel_hosts_``
  // mutation (a channel is owned by exactly one host, so it lives in exactly one host's set).
  void addHostChannel(const Upstream::HostConstSharedPtr& host, const std::string& channel) {
    if (host != nullptr) {
      host_channels_[host].insert(channel);
    }
  }
  void removeHostChannel(const Upstream::HostConstSharedPtr& host, const std::string& channel) {
    if (host == nullptr) {
      return;
    }
    auto it = host_channels_.find(host);
    if (it != host_channels_.end() && it->second.erase(channel) > 0 && it->second.empty()) {
      host_channels_.erase(it);
    }
  }
  // Drop every channel→host mapping served by ``host`` (and its count-index entry); a null host
  // releases ownership of EVERYTHING. This only RELEASES ownership — it is NOT the re-subscribe
  // signal. Under D1 the explicit ``pending_resubscribe_channels_`` set is the retry scope (seeded
  // by markHostChannelsForResubscribe, which KEEPS ownership through the backoff window so a stale
  // ack stays correlatable), and doResubscribe iterates that set, not the owner-less subset of
  // ``channel_hosts_``. Call this only where ownership must genuinely be released: a removed host,
  // or a slot that truly migrated. The control FIFO is deliberately NOT cleared here — a
  // connection-loss caller clears it separately (forgetHostConnectionLedger / dropHost), while the
  // connection-kept control-error paths must keep the rest of the host's outstanding commands.
  void forgetHostChannels(const Upstream::HostConstSharedPtr& host) {
    if (host == nullptr) {
      channel_hosts_.clear();
      host_channels_.clear();
      return;
    }
    // Drop exactly this host's channels via its reverse index — O(host's channels), not an O(all
    // channels) scan of channel_hosts_. Erasing from channel_hosts_ does not touch the set we
    // iterate (a different map); the whole host entry is erased after.
    auto it = host_channels_.find(host);
    if (it != host_channels_.end()) {
      for (const auto& channel : it->second) {
        channel_hosts_.erase(channel);
      }
      host_channels_.erase(it);
    }
  }
  // Record a just-sent SSUBSCRIBE for ``channel`` to ``host`` as the channel's CURRENT attempt:
  // allocate a fresh generation, store {host, generation} as the owner, keep the host->channels
  // index in lockstep, and push a generation-tagged entry onto the host's control FIFO so the
  // eventual ack can be correlated back to THIS attempt. Returns the generation. The single place
  // these three pieces of per-attempt state are set together, so they cannot drift.
  uint64_t recordSsubscribeAttempt(const std::string& channel,
                                   const Upstream::HostConstSharedPtr& host) {
    // If the channel already has a recorded owner, forget it FIRST so the previous host's channel
    // count is decremented before the new host's is incremented (G12). Under D1 (owners are KEPT
    // through the backoff window) this is a LIVE path, not a defensive no-op: doResubscribe
    // re-issues a channel that still holds its owner, so a SAME-HOST re-resolve (A -> A, the common
    // backoff retry) arrives here owned — this forget is what re-stamps its generation while
    // keeping host_channels_ balanced (remove then re-add the channel to the same host's set, net
    // no-op). Callers that reroute to a DIFFERENT host forget + SUNSUBSCRIBE the old owner
    // themselves before calling us (onClusterTopologyChange, and reissueSsubscribe's cross-host
    // branch — E-1), so for them the channel is already ownerless and this is the no-op tail.
    // (There is no longer a multi-channel span caller — subscribeChannels was removed — so
    // {"a","a"} is not a case.)
    if (channel_hosts_.contains(channel)) {
      forgetChannelHost(channel);
    }
    const uint64_t generation = ++next_ssubscribe_generation_;
    channel_hosts_[channel] = ChannelOwner{host, generation};
    addHostChannel(host, channel);
    control_ledger_.record(host, "ssubscribe", channel, generation);
    return generation;
  }
  // Does an upstream SSUBSCRIBE ack for ``channel`` from ``host`` — whose control-FIFO entry
  // carried
  // ``acked_generation`` — belong to the channel's CURRENT attempt? True only when the channel
  // still has a recorded owner AND that owner's host and generation both match. This is the one
  // predicate that rejects (a) an owner-less-gap ack (no owner), (b) a wrong-host ack, and (c) a
  // SAME-HOST stale ack from a superseded attempt (A -> B -> A). ``host`` is always non-null (S-4):
  // the ack's source host is mandatory, so this defense is never bypassed.
  bool ssubscribeAckIsCurrent(const std::string& channel, const Upstream::HostConstSharedPtr& host,
                              std::optional<uint64_t> acked_generation) const {
    if (!acked_generation.has_value()) {
      return false;
    }
    auto it = channel_hosts_.find(channel);
    return it != channel_hosts_.end() && it->second.host == host &&
           it->second.generation == *acked_generation;
  }
  // Forget the channel->host mapping for ``channel`` (with its host->channels index entry) if
  // present, returning the host it was mapped to (nullptr if none). The return lets a caller that
  // must then SUNSUBSCRIBE the old owner do "forget + get owner" in one call (C4). Used wherever a
  // channel's recorded owner becomes stale: doResubscribe drops it before re-resolving, an
  // unsolicited upstream SUNSUBSCRIBE (slot migration) marks it for re-resolution, and the
  // unsubscribe / removeSubscriber / topology-reroute paths use the returned host to send the
  // SUNSUBSCRIBE.
  Upstream::HostConstSharedPtr forgetChannelHost(const std::string& channel) {
    auto it = channel_hosts_.find(channel);
    if (it == channel_hosts_.end()) {
      return nullptr;
    }
    const Upstream::HostConstSharedPtr host = it->second.host;
    removeHostChannel(host, channel);
    // Erasing the whole entry drops the generation too, so a later ack for a now-forgotten channel
    // (owner-less gap) finds no current attempt to match and is rejected.
    channel_hosts_.erase(it);
    return host;
  }

  // Send an upstream SUNSUBSCRIBE and record it on the host's outstanding-control FIFO — the single
  // ledger onPushMessage consults to tell OUR advisory ack apart from an UNSOLICITED sunsubscribe
  // Redis pushes when a slot migrates off a node (A-4/S-1). AckExpected: the (sunsubscribe,
  // channel) entry sits on the FIFO until its ack (or an error) pops it, so the entry's presence at
  // the head IS the expected-ack bookkeeping — no separate per-(host,channel) count is kept.
  // ConnectionRetired (sending the host's LAST channel retired the connection inline): no ack/error
  // will ever arrive for anything queued on this host, so drop its whole FIFO rather than leave
  // stale heads that would black out a future subscribe routed back here. One of the four retire
  // paths that clear the ledger off the pool's REPORTED decision (ALT-1) — here the
  // ``ConnectionRetired`` result; the other three off retireSubscriptionConnectionIfIdle's return.
  void sendSunsubscribe(const std::string& channel, const Upstream::HostConstSharedPtr& host) {
    const UpstreamSubscriptionCallbacks::SunsubscribeResult result =
        upstream_callbacks_.sendUpstreamSunsubscribe(channel, host);
    if (host == nullptr) {
      return;
    }
    switch (result) {
    case UpstreamSubscriptionCallbacks::SunsubscribeResult::AckExpected:
      control_ledger_.record(host, "sunsubscribe", channel);
      break;
    case UpstreamSubscriptionCallbacks::SunsubscribeResult::ConnectionRetired:
      control_ledger_.clear(host);
      break;
    case UpstreamSubscriptionCallbacks::SunsubscribeResult::NotSent:
      break;
    }
  }

  // Drop ``channel`` from the re-subscribe retry scope (pending_resubscribe_channels_). If that
  // EMPTIES the scope, the whole pending re-subscribe generation is resolved — whether by a
  // current-attempt ack, an unsubscribe / downstream disconnect, or a since-unsubscribed channel
  // dropped in doResubscribe — so reset the backoff to the floor and disarm the generation timer.
  // Without this, an unrelated later host/channel loss would start its FIRST retry at the escalated
  // backoff, and a stale generation timer would fire a no-op. The ONLY site that resets/disarms on
  // the empty transition: every erase of a scope entry must go through here so the two stay in
  // sync.
  void forgetPendingResubscribe(const std::string& channel) {
    if (pending_resubscribe_channels_.erase(channel) > 0 && pending_resubscribe_channels_.empty()) {
      resubscribe_backoff_.reset();
      if (resubscribe_generation_timer_ != nullptr) {
        resubscribe_generation_timer_->disableTimer();
      }
    }
  }

  // Shared epilogue for a set of now fully-unsubscribed (orphaned) channels — used by both
  // ``unsubscribe()`` (explicit UNSUBSCRIBE) and ``removeSubscriber()`` (downstream disconnect),
  // which previously open-coded the identical loop. For each orphan: drop any outstanding
  // resubscribe-generation entry so a now-gone channel cannot keep the generation permanently
  // incomplete (F7) — and reset the backoff/timer if it was the last (forgetPendingResubscribe) —
  // then, if this registry still owned the channel→host mapping, send the upstream SUNSUBSCRIBE.
  // forgetChannelHost erases the mapping BEFORE the send so any maybeCleanupSubscriptionMode along
  // the send path observes the up-to-date map (C4). The erase is unconditional because an
  // owner-less (mid-re-resolve) orphan has no channel_hosts_ entry but may still sit in the retry
  // scope.
  void sunsubscribeOrphanedChannels(const std::vector<std::string>& orphaned) {
    for (const auto& channel : orphaned) {
      ENVOY_LOG(debug, "redis: sunsubscribing orphaned channel '{}' from shard", channel);
      forgetPendingResubscribe(channel);
      if (auto host = forgetChannelHost(channel)) {
        sendSunsubscribe(channel, host);
      }
    }
  }

  // --- Per-host outstanding control-command FIFO (upstream error correlation) ---
  // SSUBSCRIBE/SUNSUBSCRIBE are fire-and-forget, so a normal Error reply on a subscription
  // connection carries no channel. Redis replies to pipelined commands in order on a single
  // connection, so the oldest still-outstanding control command on that host is the one the error
  // is for. We record each command here on send and drain it in order: an ack (onPushMessage, with
  // the source host) pops the matching head, and an error (onUpstreamControlError) takes the head
  // to learn which channel/verb failed. Only used when the source host is known (production);
  // registry unit tests that inject acks without a host leave the FIFO untouched.
  // The per-host outstanding-control FIFO is owned by ControlCommandLedger (D3 — an independently
  // unit-testable component); the registry drives it directly via ``control_ledger_`` (record /
  // consumeAck / takeReply / clear). The Error path uses takeReply's returned command to fail its
  // channel, while the non-Error path (Finding 2) discards it and just re-resolves on backoff —
  // both pop exactly one head to keep the FIFO in lockstep with the in-order reply stream.
  using PendingControlCommand = ControlCommandLedger::PendingControlCommand;

  UpstreamSubscriptionCallbacks& upstream_callbacks_;
  Random::RandomGenerator& random_;
  // The event dispatcher (mandatory — S-4): drives the subscribe-ack timeout and resubscribe-
  // generation timers and is the registry's monotonic time source. The conn pool passes its
  // thread-local dispatcher; unit tests inject a mock. Formerly a nullable pointer whose null case
  // silently disabled the whole timeout path in tests — a second, test-only state machine.
  Event::Dispatcher& dispatcher_;
  SubsMap subscriptions_; // ``SSUBSCRIBE``
  // Tracks which upstream host serves each per-shard channel for proper ``SUNSUBSCRIBE`` routing
  // and topology change handling.
  absl::flat_hash_map<std::string, ChannelOwner> channel_hosts_;
  // Monotonic source of SSUBSCRIBE attempt generations (see ChannelOwner). Bumped once per upstream
  // SSUBSCRIBE send; never reset, so a generation value is never reused and a stale ack can always
  // be told apart from the current attempt's, even on the same host.
  uint64_t next_ssubscribe_generation_{0};
  // The set of channels in ``channel_hosts_`` served by each host — the O(1) index behind
  // ``hostHasSubscriptions`` AND the reverse index that makes markHostChannelsForResubscribe /
  // forgetHostChannels O(that host's channels) instead of an O(all channels) scan of
  // channel_hosts_. A host is dropped from the map when its last channel is forgotten (empty sets
  // are never kept), so
  // ``contains(host)`` is exactly "host still serves a channel". Kept in lockstep with every
  // channel_hosts_ mutation (addHostChannel / removeHostChannel at the same two sites).
  absl::flat_hash_map<Upstream::HostConstSharedPtr, absl::flat_hash_set<std::string>>
      host_channels_;
  // Per-host FIFO of outstanding SSUBSCRIBE/SUNSUBSCRIBE control commands in send order (extracted
  // to ControlCommandLedger — D3). It is the SINGLE ledger for both (a) correlating a channel-less
  // upstream error to the channel/verb it failed and (b) distinguishing our own SUNSUBSCRIBE
  // advisory ack from an unsolicited invalidation (A-4/S-1): a ``sunsubscribe`` entry at the head
  // is an outstanding expected ack. Kept in exact lockstep with Redis's per-connection ack/error
  // order — every ack and every error pops the head.
  ControlCommandLedger control_ledger_;
  // These three alias the single source of truth in Common::Redis::Client (client.h) so the ctor's
  // default arguments below stay in lockstep with the conn-pool runtime defaults and the filter
  // config validator — no independent literal to drift (§6).
  static constexpr uint32_t kMaxResubscribeBackoffMs =
      Common::Redis::Client::kDefaultResubscribeBackoffMaxMs;
  static constexpr uint32_t kInitialResubscribeBackoffMs =
      Common::Redis::Client::kDefaultResubscribeBackoffBaseMs;
  // A subscribe with no upstream ack within this window is failed and rolled back (F4). Generous
  // enough that a working upstream — even through a short resubscribe backoff — acks first; a
  // silent or persistently-rejecting upstream errors out here instead of hanging the SUBSCRIBE
  // forever.
  static constexpr uint32_t kSubscribeAckTimeoutMs =
      Common::Redis::Client::kDefaultSubscribeAckTimeoutMs;
  // The subscribe-ack timeout actually in force: kSubscribeAckTimeoutMs unless overridden via
  // ConnPoolSettings.pubsub_settings (A-7). Used for both the per-bucket subscribe-ack deadline and
  // the re-subscribe generation timeout.
  const std::chrono::milliseconds subscribe_ack_timeout_;
  // Channel homing policy (§7 P3). The conn pool passes the EFFECTIVE value — SHARD_MEMBERS is
  // already degraded to Primary here for a non-cluster upstream — so recordedOwnerValid /
  // resolvePlacement branch on it without re-checking the upstream kind.
  const Common::Redis::Client::SubscriptionPlacement placement_;
  // Jittered exponential backoff (default 100ms .. 30s; tunable via pubsub_settings — A-7) for
  // re-subscribing after an upstream connection loss. Reset when the re-subscribe retry scope
  // empties (forgetPendingResubscribe): the last outstanding channel's current-attempt SSUBSCRIBE
  // is acked (onPushMessage) OR it is unsubscribed/removed. A successful fire-and-forget send is
  // NOT proof of acceptance, so a permanently-rejecting upstream (Redis <7, ACL/CROSSSLOT)
  // escalates to the cap instead of hot-looping at the floor.
  JitteredExponentialBackOffStrategy resubscribe_backoff_;
  // The EXPLICIT re-subscribe retry scope (D1): the exact set of channels the next doResubscribe()
  // must re-issue. SEEDED AT SIGNAL TIME — a connection loss, unsolicited SUNSUBSCRIBE, control
  // error, or topology change inserts exactly the channels it needs re-resolved (via
  // markHostChannelsForResubscribe / scheduleResubscribe) while KEEPING their channel_hosts_ owner,
  // replacing the pre-D1 model where doResubscribe inferred the set from owner-less-ness. An entry
  // clears when its upstream ssubscribe ack lands (or the channel is fully unsubscribed). The
  // backoff resets only when this empties (the whole generation succeeded), so a partial failure —
  // one channel keeps -ERR-ing and closing the connection while others ack — keeps escalating
  // instead of resetting to the floor on every cycle (F2).
  absl::flat_hash_set<std::string> pending_resubscribe_channels_;
  // Timeout for the current re-subscribe generation (Issue 3): fires if any channel in
  // ``pending_resubscribe_channels_`` is still unacked when it elapses, driving a backoff retry.
  // Created lazily on the first armResubscribeGenerationTimer and disabled when the generation
  // completes / on clear().
  Event::TimerPtr resubscribe_generation_timer_;
  // Reused across message fan-outs to >= 2 subscribers so a fan-out does not allocate a fresh
  // encoder and output buffer per message on the delivery hot path (E3). Encoder is pinned to RESP3
  // in the ctor; the buffer is drained after each fan-out. Not re-entrant, but a message fan-out
  // never nests one.
  Common::Redis::EncoderImpl fanout_encoder_;
  Buffer::OwnedImpl fanout_buffer_;
  // Reused snapshot of the fan-out target subscribers, so a channel with > 8 subscribers does not
  // heap-allocate a fresh snapshot vector per message — the reserved capacity carries across
  // messages (E-3). Same non-nesting invariant as fanout_encoder_/fanout_buffer_ above; cleared
  // after each fan-out so it never pins subscribers between messages.
  absl::InlinedVector<DownstreamSubscriberPtr, 8> fanout_targets_;

  // Shared subscribe-ack timeout scheduler (E4, extracted — D3). Declared LAST so its constructor
  // runs after dispatcher_ and subscribe_ack_timeout_ (which it captures by reference/value). Its
  // live-predicate reads pending_subscribe_acks_ and its expiry callback drives
  // handleSubscribeAckTimeout — both invoked only on a timer fire, so their captured ``this`` is
  // fully constructed by then.
  SubscribeAckDeadlineScheduler ack_scheduler_;
};

using SubscriptionRegistryPtr = std::shared_ptr<SubscriptionRegistry>;

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
