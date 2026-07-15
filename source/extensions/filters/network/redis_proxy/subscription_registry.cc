#include "source/extensions/filters/network/redis_proxy/subscription_registry.h"

#include <algorithm>
#include <string>
#include <vector>

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/common/redis/utility.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

namespace {
// A RESP element carries a string payload as either a BulkString ($) or a SimpleString (+). Redis
// pushes channel names / verbs as either, so every Push-frame field check accepts both.
bool isRespString(const Common::Redis::RespValue& value) {
  return value.type() == Common::Redis::RespType::BulkString ||
         value.type() == Common::Redis::RespType::SimpleString;
}
} // namespace

Common::Redis::RespValue makeSubscriptionAck(const std::string& command_name,
                                             const std::string* target,
                                             int64_t subscription_count) {
  Common::Redis::RespValue ack;
  ack.type(Common::Redis::RespType::Push);

  std::vector<Common::Redis::RespValue> ack_array(3);
  ack_array[0] = Common::Redis::Utility::makeBulkString(command_name);
  if (target != nullptr) {
    ack_array[1] = Common::Redis::Utility::makeBulkString(*target);
  } else {
    ack_array[1].type(Common::Redis::RespType::Null);
  }
  ack_array[2] = Common::Redis::Utility::makeInteger(subscription_count);
  ack.asArray().swap(ack_array);

  return ack;
}

// --- DownstreamSubscriber ---

DownstreamSubscriber::DownstreamSubscriber(Network::Connection& connection,
                                           const DownstreamSubscriberStats& stats)
    : connection_(connection), stats_(stats) {
  encoder_.setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3);
}

DownstreamSubscriber::~DownstreamSubscriber() {
  // Drain the gauge for any channels still owned at destruction. Normal
  // unsubscribe/disconnect already decremented via removeChannel, leaving this a no-op; but
  // SubscriptionRegistry::clear() empties its channel->subscriber map BEFORE closing the
  // subscriber, so the close-driven removeSubscriber finds nothing to remove and those channels
  // linger in ``subscribed_channels_`` until this destructor. Decrementing here keeps the "gauge ==
  // sum of live subscribers' channel counts" invariant across cluster removal without the
  // filter/clear() ordering contract the old explicit subtract depended on.
  if (!subscribed_channels_.empty()) {
    stats_.active_subscriptions.sub(subscribed_channels_.size());
  }
}

void DownstreamSubscriber::close() {
  // NoFlush is intentional: the cluster the subscriber was bound to is gone, so buffered Push
  // frames have nowhere meaningful to land. ConnectionImpl::close(NoFlush) raises LocalClose
  // SYNCHRONOUSLY, so a caller driving close in a teardown loop must have already settled any
  // registry state the resulting ProxyFilter::onEvent will read — notably the per-registry
  // subscriber maps SubscriptionRegistry::clear() empties before closing, so the onEvent
  // UNSUBSCRIBE walk no-ops for this registry. The gauge itself needs no ordering: it is drained by
  // ~DownstreamSubscriber.
  if (connection_.state() == Network::Connection::State::Open) {
    connection_.close(Network::ConnectionCloseType::NoFlush);
  }
}

void DownstreamSubscriber::deliver(const Common::Redis::RespValue& message) {
  // A single out-of-band ack is appendAck (encode into the reused batch buffer) + flushAckBatch
  // (single write): the same encode-then-single-write path and no-count semantics the
  // multi-channel batch acks use — there is no separate deliverBatch primitive. No yield
  // separates the two, so their identical Open checks coincide. These are out-of-band control acks,
  // NOT counted in pubsub_push_messages_delivered (message frames only).
  appendAck(message);
  flushAckBatch();
}

void DownstreamSubscriber::appendAck(const Common::Redis::RespValue& ack) {
  // Encode the ack into the shared buffer without writing. The sole caller, deliver(), pairs this
  // with an immediate flushAckBatch() (atomic append+write, no yield between), so the buffer never
  // carries ack bytes across a call — the two stay split only to mirror the message-delivery
  // encode/flush shape. Do NOT revive a deferred multi-append batch on this buffer without
  // accounting for the message-delivery paths that also use it (deliverMessage / deliverSharedFrame
  // assume it is empty on entry; see their ASSERTs). Skip once the connection is gone —
  // flushAckBatch drains whatever accumulated.
  if (connection_.state() != Network::Connection::State::Open) {
    return;
  }
  encoder_.encode(ack, encode_buffer_);
}

void DownstreamSubscriber::flushAckBatch() {
  if (encode_buffer_.length() == 0) {
    return;
  }
  if (connection_.state() == Network::Connection::State::Open) {
    connection_.write(encode_buffer_, false); // write() drains the buffer
  } else {
    // Connection closed mid-batch: discard so no stale bytes prefix a later batch on this buffer.
    encode_buffer_.drain(encode_buffer_.length());
  }
}

void DownstreamSubscriber::deliverMessage(const Common::Redis::RespValue& message) {
  if (connection_.state() != Network::Connection::State::Open) {
    return;
  }
  // The shared buffer carries nothing between calls (routePush moves/writes it out, and appendAck
  // is only ever paired with an immediate flush), so a MESSAGE never inherits stale ack bytes as a
  // prefix. Assert it, so reviving a deferred-ack batch cannot silently corrupt a push.
  ASSERT(encode_buffer_.length() == 0);
  encoder_.encode(message, encode_buffer_);
  if (encode_buffer_.length() == 0) {
    return;
  }
  routePush();
}

void DownstreamSubscriber::deliverSharedFrame(const std::shared_ptr<const std::string>& bytes) {
  if (connection_.state() != Network::Connection::State::Open || bytes->empty()) {
    return;
  }
  // Same invariant as deliverMessage: the shared buffer is empty on entry, so the fan-out fragment
  // is not appended behind stale ack bytes.
  ASSERT(encode_buffer_.length() == 0);
  // Reference the shared encode via a zero-copy fragment instead of copying the payload into this
  // subscriber's buffer: the whole fan-out then costs ONE buffer->string copy (in
  // deliverFrameToSubscribers) plus a small per-subscriber fragment, not N full payload copies. The
  // releasor captures ``bytes`` so the shared string outlives the fragment, and deletes the
  // heap-allocated fragment once the connection (or the FIFO park buffer it is moved into) drains
  // it — including on teardown, when the owning buffer's destructor drains its fragments.
  auto* fragment = new Buffer::BufferFragmentImpl(
      bytes->data(), bytes->size(),
      [bytes](const void*, size_t, const Buffer::BufferFragmentImpl* frag) { delete frag; });
  // Reuse the subscriber's member buffer (as deliverMessage does) instead of a per-delivery
  // local OwnedImpl: every path below drains it (the sink MOVES its slice out, or write() drains
  // it), so it carries nothing between fan-out deliveries and holds only this one fragment while in
  // use.
  encode_buffer_.addBufferFragment(*fragment);
  routePush();
}

void DownstreamSubscriber::routePush() {
  if (auto sink = ordering_sink_.lock()) {
    // Route through the owning filter so the push lands after any in-flight command replies (FIFO).
    // enqueueOrderedPush MOVES the bytes out of the reusable member buffer (no copy) and leaves it
    // empty for the next push — no explicit drain needed. Do NOT count here: the frame may be
    // parked and later dropped on slow-subscriber eviction / disconnect, so the filter counts it at
    // ACTUAL delivery (enqueueOrderedPush's straight-to-wire fast path, or flushReadyResponses when
    // a parked frame flushes).
    sink->enqueueOrderedPush(encode_buffer_);
  } else {
    // No sink (registry-only tests) or the filter is gone: legacy direct write (write() drains) —
    // delivered now, so count it.
    connection_.write(encode_buffer_, false);
    stats_.push_delivered.inc();
  }
}

// --- SubscriptionRegistry ---

SubscriptionRegistry::SubscriptionRegistry(UpstreamSubscriptionCallbacks& upstream_callbacks,
                                           Random::RandomGenerator& random,
                                           Event::Dispatcher& dispatcher,
                                           std::chrono::milliseconds subscribe_ack_timeout,
                                           std::chrono::milliseconds resubscribe_backoff_base,
                                           std::chrono::milliseconds resubscribe_backoff_max)
    : upstream_callbacks_(upstream_callbacks), random_(random), dispatcher_(dispatcher),
      subscribe_ack_timeout_(subscribe_ack_timeout),
      resubscribe_backoff_(resubscribe_backoff_base.count(), resubscribe_backoff_max.count(),
                           random_),
      ack_scheduler_(
          dispatcher_, subscribe_ack_timeout_,
          [this](const std::string& channel, uint64_t seq) {
            // Live iff the channel still has a pending bucket registered under this exact token.
            auto it = pending_subscribe_acks_.find(channel);
            return it != pending_subscribe_acks_.end() && it->second.schedule_seq_ == seq;
          },
          [this](const std::string& channel) { handleSubscribeAckTimeout(channel); }) {
  fanout_encoder_.setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3);
}

// --- Private helpers ---

SubscriptionRegistry::AddSubscriptionResult
SubscriptionRegistry::addSubscription(const std::string& channel,
                                      const DownstreamSubscriberPtr& subscriber) {
  // addChannel inserts into the subscriber's set and reports whether the channel was ABSENT before
  // — the same fact subscribe() previously computed with a separate ``subscribedChannels().
  // contains(channel)`` pre-check, hashing the key twice. Take it from the insert result instead.
  const bool subscriber_newly_added = subscriber->addChannel(channel);
  auto& subscribers = subscriptions_[channel];
  auto [_, inserted] = subscribers.insert(subscriber);
  // Newly owned by THIS registry (needs an upstream SSUBSCRIBE) iff this subscriber was the 0 -> 1
  // transition for the channel.
  return {subscriber_newly_added, inserted && subscribers.size() == 1};
}

std::vector<std::string>
SubscriptionRegistry::removeSubscriptions(absl::Span<const std::string> keys,
                                          const DownstreamSubscriberPtr& subscriber) {
  std::vector<std::string> orphaned;
  for (const auto& key : keys) {
    // Only drop the key from the subscriber's shared set (which spans EVERY registry the subscriber
    // touches) when THIS registry actually owned this channel for this subscriber. A multi-cluster
    // subscriber's UNSUBSCRIBE walks all tracked registries; a non-owning registry must leave the
    // shared set — and thus the subscriber's total count — untouched, otherwise the owning registry
    // is skipped (the splitter stops once the count drops) and the real subscription is stranded
    // with no upstream ``SUNSUBSCRIBE``.
    auto it = subscriptions_.find(key);
    if (it == subscriptions_.end() || it->second.erase(subscriber) == 0) {
      continue;
    }
    subscriber->removeChannel(key);
    if (it->second.empty()) {
      subscriptions_.erase(it);
      orphaned.push_back(key);
    }
  }
  return orphaned;
}

void SubscriptionRegistry::clear() {
  // Snapshot affected subscribers before we mutate any state. Used to close the downstream
  // connections after we've cleared the registry-side maps, so the close-induced
  // ProxyFilter::onEvent walk finds the registry empty.
  absl::flat_hash_set<DownstreamSubscriberPtr> affected_subscribers;
  for (const auto& [_, subscribers] : subscriptions_) {
    affected_subscribers.insert(subscribers.begin(), subscribers.end());
  }

  // Drop registry-side state first. Pending upstream subscribe-acks weak-ref the subscribers
  // we're about to close and target channels the registry no longer owns; clearing them now
  // means any in-flight ack that races us decays into a no-op rather than firing a fabricated
  // subscribe ack on a torn-down connection.
  subscriptions_.clear();
  channel_hosts_.clear();
  host_channels_.clear();
  pending_subscribe_acks_.clear();
  // Drop the shared subscribe-ack schedule with the buckets it tracked and disable its timer, so a
  // pending wake does not fire against a torn-down registry.
  ack_scheduler_.clear();
  control_ledger_.clearAll();
  pending_resubscribe_channels_.clear();
  resetResubscribeCycle();

  // Close affected downstream connections last, after the registry maps above are emptied. The
  // active-subscriptions gauge is owned by the subscriber now: the channels THIS registry
  // held are drained by ~DownstreamSubscriber when the subscriber is destroyed — the local
  // ``affected_subscribers`` set is the last strong ref, so that drain runs as this function
  // returns, off the subscriber's own ``subscribed_channels_`` (which clear() never touches). There
  // is no longer an "onEvent recomputes the decrement from the registry-side sets" ordering
  // contract.
  // ``ConnectionImpl::close(NoFlush)`` raises LocalClose synchronously → ProxyFilter::onEvent walks
  // the tracked registries to send ``UNSUBSCRIBE`` upstream: for THIS registry the maps were just
  // emptied so the walk no-ops (and no in-flight ack can fabricate a subscribe ack on the torn-down
  // connection); OTHER tracked registries (multi-cluster fan-out) clean up normally via the same
  // removeSubscriber path they use on any disconnect.
  for (const auto& subscriber : affected_subscribers) {
    subscriber->close();
  }
}

void SubscriptionRegistry::removeSubscriber(const DownstreamSubscriberPtr& subscriber) {
  dropPendingForSubscriber(subscriber);

  // Walk the subscriber's OWN (small) subscription set and hash-erase each key from THIS
  // registry's map — O(k) in the subscriber's channel count, not O(N) in the thread-wide distinct
  // channel count (a 2-channel client disconnecting on a 50k-channel proxy now touches 2 buckets,
  // not 50k). ``subscribed_channels_`` mirrors the subscriber's membership across ALL
  // tracked registries (a subscriber may span clusters, and ProxyFilter::onEvent calls
  // removeSubscriber once per registry), so only erase the subscriber-set entries this registry
  // actually owned — collect them during the read-only walk and erase after, leaving other
  // registries' channels intact for their own removeSubscriber pass.
  std::vector<std::string> orphaned;
  std::vector<std::string> owned;
  for (const auto& channel : subscriber->subscribedChannels()) {
    auto it = subscriptions_.find(channel);
    if (it != subscriptions_.end()) {
      if (it->second.erase(subscriber) == 0) {
        // this registry never owned THIS subscriber for THIS channel — a multi-registry
        // subscriber whose channel was re-homed to a sibling registry (listener re-config), and the
        // sibling's map still holds the subscriber. Do NOT touch the shared subscribed_channels_ or
        // the gauge here (that would over-decrement and strand the subscriber's real reference in
        // the owning registry); the OWNING registry's own removeSubscriber pass removes it.
        // Symmetric with removeSubscriptions' identical erase == 0 guard.
        continue;
      }
      owned.push_back(channel);
      if (it->second.empty()) {
        orphaned.push_back(channel);
        subscriptions_.erase(it);
      }
    }
  }
  for (const auto& channel : owned) {
    subscriber->removeChannel(channel);
  }

  sunsubscribeOrphanedChannels(orphaned);
}

SubscriptionRegistry::SubscribeResult
SubscriptionRegistry::subscribe(const std::string& channel,
                                const DownstreamSubscriberPtr& subscriber,
                                const SubscriptionAckTargetWeakPtr& ack_target) {
  // addSubscription mirrors the channel into the subscriber's set and returns whether THIS registry
  // now newly owns it (0 -> 1 subscribers). One channel per call, so there is no batch step
  // machinery: the ack count is simply the subscriber's total AS OF this channel (Redis's per-step
  // semantics), which is correct by construction — removing the fresh+dedup step skew.
  // ``subscriber_newly_added`` is whether THIS call newly added the channel to the subscriber's OWN
  // set — false for a DUPLICATE SUBSCRIBE from a subscriber that already holds it. It is threaded
  // onto the pending-ack entry (below): a failed (re-)subscribe rolls back ONLY the entries that
  // newly established a subscription, never a duplicate whose live subscription it merely echoes
  // A duplicate still PARKS (so it is never acked prematurely), it is just not torn
  // down on failure. addSubscription reports it from the set-insert result so the key is hashed
  // once, not once for a ``contains`` pre-check plus again on insert.
  const auto [subscriber_newly_added, first_subscriber] = addSubscription(channel, subscriber);
  const uint64_t count = subscriber->totalSubscriptionCount();
  if (first_subscriber) {
    if (!subscriber_newly_added) {
      // Invariant guard: THIS registry is the first subscriber for the channel, yet the subscriber
      // already holds it — so it is subscribed via ANOTHER registry (a cluster-update registry swap
      // that bypassed the connection's per-channel owner map). Do NOT establish a SECOND upstream
      // subscription. Back the channel out of THIS registry's map ONLY — registry-scoped, leaving
      // the subscriber's shared channel set alone: addSubscription's addChannel returned false, so
      // this call never added it there, and the OWNING registry still holds the real reference.
      // Touching the shared set here (e.g. via removeSubscriptions) would strand that owner — an
      // over-decremented gauge and a leaked upstream SSUBSCRIBE. Ack immediately, exactly like a
      // dedup on an already-active channel. The splitter's owner-first routing makes this
      // unreachable in normal operation; the guard keeps any future bypass from corrupting the
      // owning registry.
      ENVOY_BUG(subscriber_newly_added,
                "pub/sub: first-subscriber for a channel the subscriber already owns via another "
                "registry; backing out registry-scoped");
      if (auto it = subscriptions_.find(channel); it != subscriptions_.end()) {
        it->second.erase(subscriber);
        if (it->second.empty()) {
          subscriptions_.erase(it);
        }
      }
      // bind_owner=false: the channel is really owned by another registry, so the splitter must NOT
      // rebind it here (that would strand the true owner on a later UNSUBSCRIBE).
      return {/*success=*/true, count, /*ack_deferred=*/false, /*bind_owner=*/false};
    }
    // First subscriber for this channel on this thread — issue the sharded upstream subscribe and
    // park the downstream ack until the upstream confirms.
    ENVOY_LOG(debug, "redis: ssubscribing channel '{}' to correct shard", channel);
    // Placement: resolve the slot owner, then send to it — the fresh-subscribe equivalent
    // of reissueSsubscribe's re-place branch. A null resolve OR a failed send rolls the optimistic
    // subscription back.
    const Upstream::HostConstSharedPtr chosen_host = resolvePlacement(channel);
    if (chosen_host == nullptr ||
        !upstream_callbacks_.sendUpstreamSsubscribeToHost(channel, *this, chosen_host)) {
      ENVOY_LOG(warn, "redis: upstream ssubscribe failed for '{}', rolling back", channel);
      removeSubscriptions(absl::MakeConstSpan(&channel, 1), subscriber);
      return {/*success=*/false, subscriber->totalSubscriptionCount(), /*ack_deferred=*/false,
              /*bind_owner=*/false};
    }
    // Park the downstream ack keyed by channel; the eventual downstream ack always emits the client
    // verb ``subscribe`` (see deliverPendingSubscribeAck) since cluster sharding is transparent.
    // ``subscriber_newly_added`` is necessarily true here — the guard at the top of this branch
    // returns for the already-owned-via-another-registry case — so a failed reissue rolls back
    // exactly the reference THIS call established, never one the subscriber holds elsewhere.
    registerPendingSubscribeAck(channel, subscriber, count, subscriber_newly_added, ack_target);
    // A non-null return is the chosen shard owner (no separate success flag), so record it
    // unconditionally as the channel's current attempt.
    recordSsubscribeAttempt(channel, chosen_host);
    return {/*success=*/true, count, /*ack_deferred=*/true, /*bind_owner=*/true};
  }
  // Dedup, but the channel may not be confirmed on its upstream yet. A SINGLE find on
  // pending_subscribe_acks_ decides which case and, for the common one, joins the bucket without a
  // second hash (the former ``contains(channel)`` + registerPendingSubscribeAck's ``[channel]``
  // hashed the same key twice). ``subscriber_newly_added`` tags whether THIS call established the
  // subscription: a DUPLICATE (false) still parks so it is never acked early, but a failed
  // (re-)subscribe will not roll back the live subscription it merely echoes.
  if (auto it = pending_subscribe_acks_.find(channel); it != pending_subscribe_acks_.end()) {
    // Still SUBSCRIBING — a prior subscriber's pending bucket is open. Join it (reuses its
    // ack-timeout schedule entry) so this subscriber is acked when the upstream ack lands, not
    // prematurely.
    appendPendingSubscribeAck(it->second, channel, subscriber, count, subscriber_newly_added,
                              ack_target);
    return {/*success=*/true, count, /*ack_deferred=*/true, /*bind_owner=*/true};
  }
  if (pending_resubscribe_channels_.contains(channel)) {
    // RE-subscribing after a connection loss / slot move (upstream ack not yet re-confirmed) —
    // there is no pending bucket yet, so open one and arm its ack timeout.
    registerPendingSubscribeAck(channel, subscriber, count, subscriber_newly_added, ack_target);
    return {/*success=*/true, count, /*ack_deferred=*/true, /*bind_owner=*/true};
  }
  // Dedup on an already-ACTIVE channel (its upstream ack has landed and drained the bucket) — no
  // upstream ack will fire for this subscriber, so the splitter fabricates the ack immediately.
  // This registry owns the active channel, so it is the correct owner to bind.
  return {/*success=*/true, count, /*ack_deferred=*/false, /*bind_owner=*/true};
}

uint64_t SubscriptionRegistry::unsubscribe(absl::Span<const std::string> channels,
                                           const DownstreamSubscriberPtr& subscriber,
                                           std::vector<Common::Redis::RespValue>* preserved_acks) {
  for (const auto& channel : channels) {
    // Redis-compatible SUBSCRIBE-then-UNSUBSCRIBE: if this channel's SUBSCRIBE is still awaiting
    // its upstream ack, complete that ``subscribe`` ack rather than silently dropping it — Redis
    // replies ``subscribe ch <n>`` then ``unsubscribe ch <n-1>`` — WITHOUT waiting for the upstream
    // (a client-cancelled subscribe; a late ack/error for the channel is thereafter ignored).
    // collectPreservedSubscribeAcks routes it: if the parked SUBSCRIBE request's ack target is
    // still live it POSTS the ack to complete that request at its own FIFO slot; otherwise it
    // COLLECTS the ack (NO downstream write) into the caller's buffer so the splitter flushes it —
    // ahead of its own unsubscribe ack — only after its terminal respond(); delivering mid-teardown
    // would let a synchronous downstream close re-enter and mutate the very pending bucket we are
    // draining (reentrancy/UAF). On an already-ACTIVE channel (bucket drained) this is a no-op. The
    // rollback (failPendingSubscribers) passes a null buffer — its bucket was already erased — and
    // simply drops the entry, so the subscriber still gets the rollback (connection close), not a
    // spurious success.
    if (preserved_acks != nullptr) {
      collectPreservedSubscribeAcks(channel, subscriber, *preserved_acks);
    } else {
      // The subscriber is abandoning this channel's pending ack; scrub its entry from the bucket.
      scrubPendingBucket(channel, subscriber);
    }
  }
  auto orphaned = removeSubscriptions(channels, subscriber);
  sunsubscribeOrphanedChannels(orphaned);
  return subscriber->totalSubscriptionCount();
}

void SubscriptionRegistry::scheduleResubscribe() {
  // Arm the backoff to drive the next doResubscribe(), which re-issues exactly the channels in the
  // EXPLICIT retry scope pending_resubscribe_channels_ — not an implicit "owner-less subset".
  // Callers that have ALREADY seeded the scope with just the channel(s) they need re-resolved — an
  // unsolicited SUNSUBSCRIBE that forgot+marked its one migrated channel, or a re-issued-SSUBSCRIBE
  // error that forgot+marked its channel — call this directly so the rest of the host's healthy
  // channels keep their mapping; whole-host callers go through scheduleResubscribeForHost.
  if (subscriptions_.empty()) {
    resetResubscribeCycle();
    return;
  }
  // Nothing to re-issue: a host-scoped signal whose host carried no channels (e.g. its subscription
  // connection was already retired at its last channel) seeds no entries. Don't advance the backoff
  // or arm the pool timer for an empty scope — doResubscribe would fire only to re-issue nothing.
  if (pending_resubscribe_channels_.empty()) {
    return;
  }
  // Coalesce: if a re-subscribe cycle is ALREADY scheduled (the pool timer is armed but has not
  // yet fired doResubscribe), this signal rides that cycle. Every signal path enrolls its
  // channel(s) in pending_resubscribe_channels_ BEFORE calling here, and doResubscribe re-reads
  // that whole set at fire time, so a coalesced signal is not lost. Returning early avoids two
  // defects a burst of signals would otherwise cause: (a) advancing the backoff once PER SIGNAL
  // rather than once per fired cycle — a K-channel slot move (K unsolicited SUNSUBSCRIBE pushes
  // in one read, or K -MOVEDs for K re-issued SSUBSCRIBE sends) would jump the FIRST retry from
  // ~100ms to ~15s; (b) enableTimer replacing the pending deadline, so a steady drip of signals
  // (e.g. the generation-timeout re-arm while a pool cycle is already pending) defers the
  // doResubscribe indefinitely. The backoff therefore advances exactly once per fired cycle — a
  // permanently-rejecting upstream still escalates to the 30s cap (doResubscribe's failure path
  // re-arms after the timer has fired, when it is no longer pending) — and is reset when the retry
  // scope empties (forgetPendingResubscribe).
  if (upstream_callbacks_.resubscribeTimerPending()) {
    return;
  }
  // Jittered exponential backoff (100ms .. 30s cap) via the shared strategy, which also spreads
  // reconnects across Envoy instances to avoid a thundering herd.
  const auto delay = std::chrono::milliseconds(resubscribe_backoff_.nextBackOffMs());
  upstream_callbacks_.scheduleResubscribe(delay);
}

void SubscriptionRegistry::markHostChannelsForResubscribe(
    const Upstream::HostConstSharedPtr& host) {
  // Seed the EXPLICIT re-subscribe signal set (pending_resubscribe_channels_) with the channels
  // this event affects, and KEEP their channel→host owner until doResubscribe actually re-issues
  // them. This replaces the former "clear the owner at signal time and let doResubscribe
  // re-send the owner-less subset" model, which opened a window (backoff 100ms..30s) where a
  // channel sat in subscriptions_ with no owner and several paths misread that gap:
  //  * an UNSUBSCRIBE / downstream disconnect could not find the owner, so it skipped the upstream
  //  SUNSUBSCRIBE and the connection's retire — leaking the subscription on a LIVE connection;
  //  keeping the owner lets forgetChannelHost return it so the SUNSUBSCRIBE is sent;
  //  * a dedup SUBSCRIBE fell through to an unguarded immediate success ack because the channel
  //  was
  //  not yet in this set; seeding it HERE makes subscribe()'s dedup gate defer + protect
  //  the joiner with the ack timeout;
  //  * an unsolicited-SUNSUBSCRIBE retire gate (hostHasSubscriptions) mistook the owner gap for
  //  "no
  //  subscription" and retired a connection still carrying channels; the owner stays, so
  //  the host still reports its channels;
  //  * a still-posted owner clear could wipe a replacement connection's fresh owner — there
  //  is no owner clear here to race.
  // Every re-subscribe signal is scoped to a SPECIFIC host (one host's loss does not
  // re-SSUBSCRIBE every channel on every other host). The owner is refreshed (new host + new
  // generation) when reissueSsubscribe re-sends the channel, so a late ack for the pre-signal
  // attempt is still correlated by ssubscribeAckIsCurrent until then. The control FIFO is untouched
  // here: connection-loss callers clear it separately (forgetHostConnectionLedger);
  // connection-kept control-error callers keep it.
  ASSERT(host != nullptr); // no whole-registry seed — every re-subscribe signal names its host
  // Seed exactly this host's channels via its reverse index — O(host's channels), not an O(all
  // channels) scan of channel_hosts_.
  auto it = host_channels_.find(host);
  if (it != host_channels_.end()) {
    for (const auto& channel : it->second) {
      pending_resubscribe_channels_.insert(channel);
    }
  }
}

void SubscriptionRegistry::scheduleResubscribeForHost(const Upstream::HostConstSharedPtr& host) {
  markHostChannelsForResubscribe(host);
  scheduleResubscribe();
}

void SubscriptionRegistry::forgetHostConnectionLedger(const Upstream::HostConstSharedPtr& host) {
  // A dropped subscription connection means this host will never ack or error the control commands
  // (SSUBSCRIBE/SUNSUBSCRIBE) we sent it. Clear its outstanding-control FIFO so a later unsolicited
  // SUNSUBSCRIBE (slot moved to a DIFFERENT host) is treated as an invalidation, not swallowed as
  // an advisory ack for a now-dead expected SUNSUBSCRIBE (the FIFO is the single ledger).
  ASSERT(host != nullptr); // a subscription connection is per-host; callers always name it
  control_ledger_.clear(host);
}

void SubscriptionRegistry::onUpstreamControlError(Common::Redis::RespValuePtr&& value,
                                                  const Upstream::HostConstSharedPtr& host) {
  // A non-Push reply on a subscription connection is Redis's error reply to a fire-and-forget
  // SSUBSCRIBE/SUNSUBSCRIBE control command (e.g. -MOVED / -CROSSSLOT / -NOPERM / -ERR unknown
  // command on Redis <7 / -OOM). We keep the shared connection open (tearing it down would re-issue
  // every channel on it and drop its healthy subscriptions) and, because Redis replies to pipelined
  // commands in order, correlate the error to the OLDEST outstanding control command on this host.
  const bool is_error = value != nullptr && value->type() == Common::Redis::RespType::Error;
  const std::string error_str = is_error ? value->asString() : "";

  // Redis answers a connection's pipelined fire-and-forget control commands IN ORDER, so EVERY
  // non-Push reply consumes the oldest outstanding command's reply slot: an -ERR here, and even an
  // anomalous non-Error (real Redis only acks a control command with a Push or answers with an
  // Error, so a bare reply is middle-box/out-of-sync noise). Pop the FIFO head for BOTH to keep the
  // ledger in lockstep with the reply stream: leaving a non-Error un-popped slips the
  // FIFO by one — the next ssubscribe ack then head-mismatches and its pending subscribe hangs to
  // the ack timeout. For a SUNSUBSCRIBE the pop also IS the drop of its expected-ack bookkeeping
  // (the FIFO is the single ledger), so a later genuine unsolicited SUNSUBSCRIBE is not
  // swallowed as a lingering advisory expectation.
  const std::optional<PendingControlCommand> reply = control_ledger_.takeReply(host);

  // A non-Error reply carries no actionable failure — unlike the Error path below, neither
  // correlate nor fail the popped command; keep the connection open and just re-resolve this host
  // on backoff. The head is already consumed above, so the FIFO stays balanced.
  if (!is_error) {
    ENVOY_LOG(debug, "redis: non-Error subscription control reply, scheduling host-scoped "
                     "re-subscribe (connection kept open)");
    scheduleResubscribeForHost(host);
    return;
  }

  // -MOVED / -ASK / -CLUSTERDOWN: the local slot map is stale, so a plain re-subscribe would just
  // draw the same redirect. Refresh the cluster topology (throttled by the shared refresh manager)
  // and re-resolve on backoff once it lands. Unlike the data path (which follows an -ASK with an
  // ASKING handshake + a retry to the migration target), the subscription control path treats -ASK
  // like -MOVED — a topology-refresh signal — with no per-channel ASKING retry: sharded pub/sub
  // signals slot moves with an unsolicited SUNSUBSCRIBE, not a per-key -ASK, so an SSUBSCRIBE
  // caught mid-migration is re-resolved when the refresh lands (or, if the migration never settles
  // the subscribe-ack timeout, closed and reconnected) rather than redirected key-by-key. Match the
  // leading error-code token against the shared wire constants (client.h) — the same tokens the
  // client uses to classify redirects.
  const auto& redirect = Common::Redis::Client::RedirectionResponse::get();
  // Match the leading error-code TOKEN (the code followed by a space or end-of-string), not a bare
  // prefix: a redirect reply is "MOVED <slot> <endpoint>" / "ASK <slot> <endpoint>" / "CLUSTERDOWN
  // <msg>", so requiring the boundary keeps a hypothetical "MOVED2 ..." / "ASKING ..." from being
  // misclassified as a redirect.
  const auto is_redirect_code = [&error_str](const std::string& code) {
    return absl::StartsWith(error_str, code) &&
           (error_str.size() == code.size() || error_str[code.size()] == ' ');
  };
  if (is_redirect_code(redirect.MOVED) || is_redirect_code(redirect.ASK) ||
      is_redirect_code(redirect.CLUSTER_DOWN)) {
    // The redirect proves the local slot map is stale, so refresh it (throttled) regardless.
    upstream_callbacks_.requestTopologyRefresh();
    // A redirect is ACTIONABLE — re-mark the correlated channel for re-subscribe on
    // the escalating backoff — ONLY when it correlates to the channel's CURRENT SSUBSCRIBE attempt
    // on this host. Ignore it otherwise (the refresh above already handles the stale slot map),
    // because:
    //  * it correlates to a SUNSUBSCRIBE (verb != "ssubscribe"): we always forgetChannelHost
    //  BEFORE
    //  sending a channel's SUNSUBSCRIBE (the channel had already moved off this host), so a
    //  -MOVED for it is STRUCTURALLY always stale. Re-marking would needlessly re-SSUBSCRIBE
    //  this host's healthy channels and open a duplicate/gap window. The generic-error branch
    //  below already ignores this exact case via its owner check
    //  (subscriptions_/ssubscribeAckIsCurrent); this removes the asymmetry;
    //  * it correlates to a SUPERSEDED SSUBSCRIBE (an A -> B -> A entry whose host matches but
    //  generation does not, or a channel that already re-resolved off this host) — the channel
    //  has already moved on.
    // The FIFO head was already consumed above, so just return. A redirect with NO correlated
    // command (empty FIFO — middle-box/out-of-sync noise) still falls through to host re-subscribe.
    if (reply.has_value() && (reply->verb != "ssubscribe" ||
                              !ssubscribeAckIsCurrent(reply->channel, host, reply->generation))) {
      ENVOY_LOG(debug,
                "redis: ignoring stale '{}' redirect for '{}' (not a current SSUBSCRIBE attempt)",
                error_str, reply->channel);
      return;
    }
    if (reply.has_value()) {
      // Scope the re-mark to the CORRELATED channel, not the whole host. Reaching here with a
      // reply means this redirect is for reply->channel's CURRENT SSUBSCRIBE attempt, so only THAT
      // channel's slot moved; the host's other channels are unaffected. Re-marking the whole host
      // would needlessly re-SSUBSCRIBE them and — during a slot migration's window before the
      // throttled slot-map refresh lands — churn every one of them each backoff cycle (each redraws
      // a redirect off the still-stale map). Enroll just this channel (KEEPING its owner) and
      // arm the backoff; the topology refresh requested above re-routes it via
      // onClusterTopologyChange, and the backoff is the safety net if that refresh never changes
      // Envoy's view of the slot map.
      ENVOY_LOG(debug,
                "redis: subscription control redirect '{}' for '{}', scheduling re-subscribe",
                error_str, reply->channel);
      pending_resubscribe_channels_.insert(reply->channel);
      scheduleResubscribe();
    } else {
      // No correlated command (empty FIFO — middle-box / out-of-sync noise): the redirect cannot be
      // attributed to a specific channel, so fall back to the whole-host re-mark.
      ENVOY_LOG(debug, "redis: uncorrelated control redirect '{}', scheduling host re-subscribe",
                error_str);
      scheduleResubscribeForHost(host);
    }
    return;
  }

  // A normal error to an SSUBSCRIBE we sent (ACL/CROSSSLOT/unknown-command): fail that channel's
  // pending downstream subscribers IMMEDIATELY (gauge rollback + connection close) instead of
  // leaving them hanging until the subscribe-ack timeout. failPendingSubscribers rolls the
  // subscription back, and the connection's other (healthy) channels are untouched — no host-scoped
  // re-subscribe needed.
  if (reply.has_value() && reply->verb == "ssubscribe") {
    // Only act on an error for the channel's CURRENT attempt — symmetric with the ack path's
    // ssubscribeAckIsCurrent gate. A STALE SSUBSCRIBE error (a superseded attempt on a
    // since-migrated host, or a same-host A -> B -> A retry whose host matches but generation does
    // not) must NOT fail a pending subscribe that is now awaiting a newer attempt's ack, nor forget
    // the current owner mapping. control_ledger_.takeReply already consumed this now-dead FIFO
    // entry above, so simply drop it.
    if (!ssubscribeAckIsCurrent(reply->channel, host, reply->generation)) {
      ENVOY_LOG(debug, "redis: ignoring stale SSUBSCRIBE error for '{}' (not the current attempt)",
                reply->channel);
      return;
    }
    std::string message = "ERR subscribe to '" +
                          Common::Redis::sanitizeControlBytes(reply->channel) +
                          "' failed: " + Common::Redis::sanitizeControlBytes(error_str);
    // Roll back any FRESH pending subscribers for this channel — joiners whose subscription this
    // failed attempt was still establishing. (A duplicate SUBSCRIBE from a subscriber that already
    // held the channel never parked — see subscribe() — so its live subscription is untouched.)
    failPendingSubscribers(reply->channel, message);
    // After the rollback, decide by whether the channel is STILL active — NOT by whether a pending
    // bucket existed. The prior "had a bucket -> return" shortcut mishandled the mixed case: an
    // ALREADY-ACTIVE channel being re-resolved whose window ALSO gathered a fresh joiner would fail
    // the joiner and then return, leaving the live subscription un-re-resolved until the generation
    // timeout. If the channel still has active subscribers here — the mixed case, or a plain
    // re-issued SSUBSCRIBE with no bucket at all — the upstream SSUBSCRIBE it needs still failed,
    // so re-resolve NOW. If the rollback removed the last subscriber (a pure fresh SUBSCRIBE),
    // there is nothing left to re-resolve.
    if (!subscriptions_.contains(reply->channel)) {
      ENVOY_LOG(
          debug,
          "redis: upstream SSUBSCRIBE for '{}' failed ('{}'), pending subscribers rolled back",
          reply->channel, error_str);
      return;
    }
    // The channel is genuinely NOT subscribed on this host now (the SSUBSCRIBE was refused), so
    // drop its stale mapping and re-resolve. It STAYS in pending_resubscribe_channels_
    // (reissueSsubscribe enrolled it before the send), so doResubscribe re-sends it and
    // subscribe()'s dedup still defers a NEW joiner — owner-lessing here is safe precisely because
    // nothing is subscribed upstream to leak (unlike the connection-loss window, where the channel
    // WAS subscribed on a live connection).
    ENVOY_LOG(debug, "redis: SSUBSCRIBE for active channel '{}' failed ('{}'), re-resolving",
              reply->channel, error_str);
    forgetChannelHost(reply->channel);
    // if that was this host's LAST channel, its dedicated subscription connection is now
    // idle. Every OTHER last-channel path retires it; do so here too instead of leaking an idle
    // connection
    // + its Redis-side state until an unrelated event closes it.
    retireHostConnectionIfIdle(host);
    scheduleResubscribe();
    return;
  }

  // A SUNSUBSCRIBE error, or an error with no correlated command: keep the connection and
  // re-resolve this host's channels on backoff. A transient error recovers; a permanent one keeps
  // escalating. (Non-Error replies returned early above, so this is always a genuine error.)
  //
  // if the error DID correlate to a specific channel this host NO LONGER owns (it already
  // re-resolved elsewhere — a stale SUNSUBSCRIBE error, or any leftover superseded attempt), the
  // operation is moot; ignore it rather than re-mark this host's healthy channels for a needless
  // re-SSUBSCRIBE. An uncorrelated error (no reply) has no channel to check and falls through to
  // the conservative host re-mark.
  if (reply.has_value()) {
    auto owner_it = channel_hosts_.find(reply->channel);
    if (owner_it == channel_hosts_.end() || owner_it->second.host != host) {
      // A plain-error reply to a SUNSUBSCRIBE we sent (the redirect cases returned above) means the
      // old owner REFUSED to drop a channel we already re-routed off it: it keeps that shard
      // subscription and keeps pushing the channel's messages — now dropped downstream by the
      // smessage source-host check, but at a real upstream + cluster-bus + parse cost per message
      // for the life of the connection. Force Redis-side cleanup by closing the source host's
      // subscription connection: Redis drops ALL its shard subscriptions on close, and the conn
      // pool re-subscribes the host's remaining HEALTHY channels via the genuine-connection-loss
      // path (deferred, no planned_removal_). Bounded and loop-free: closing clears the host's
      // control ledger (no lingering entry re-triggers this), and every re-issue is an SSUBSCRIBE
      // (handled by the ssubscribe path), never another SUNSUBSCRIBE. Only a SUNSUBSCRIBE error
      // takes this branch — a stale SSUBSCRIBE error is handled above, and a channel whose slot
      // genuinely MOVED (-MOVED/-ASK) returned in the redirect branch, since Redis cleans that up
      // server-side.
      if (reply->verb == "sunsubscribe") {
        ENVOY_LOG(debug,
                  "redis: SUNSUBSCRIBE for '{}' failed ('{}'); closing the source host's "
                  "subscription connection to reclaim the leaked shard subscription",
                  reply->channel, error_str);
        upstream_callbacks_.closeSubscriptionConnection(host);
        return;
      }
      ENVOY_LOG(debug, "redis: ignoring stale control error for '{}' (host no longer owns it)",
                reply->channel);
      return;
    }
  }
  ENVOY_LOG(debug,
            "redis: upstream subscription control error '{}', scheduling host-scoped re-subscribe "
            "(connection kept open)",
            error_str);
  scheduleResubscribeForHost(host);
}

void SubscriptionRegistry::doResubscribe() {
  // If all subscriptions were cleared (e.g., cluster removed, all subscribers disconnected),
  // stop retrying. Prevents zombie retry loops after cluster removal.
  if (empty()) {
    resetResubscribeCycle();
    // No resubscribe scope needs clearing: the retry scope is the EXPLICIT
    // pending_resubscribe_channels_ set, not the owner-less subset of channel_hosts_, and once
    // subscriptions_ is empty any stale entry is inert — doResubscribe re-issues nothing for a
    // channel with no live subscription. A later single-host loss thus cannot widen into a
    // whole-registry storm (no all-flag / pending-host field survives — the former
    // whole-registry-storm hazard).
    return;
  }

  // Re-issue exactly the channels in the EXPLICIT re-subscribe signal set: every signal path
  // (connection loss, control error, topology fallback, unsolicited invalidation) seeds
  // pending_resubscribe_channels_ AT SIGNAL TIME, so this set — not an implicit "owner-less subset
  // of channel_hosts_" — IS the "which channels to re-send" scope. That is what keeps the owner
  // alive through the backoff window. Snapshot first:
  // reissueSsubscribe re-inserts each channel (idempotent), so iterating the live set would
  // invalidate under us. A channel that was fully unsubscribed since the signal is dropped from the
  // set here (unsubscribe / removeSubscriber already erase it, so this is belt-and-suspenders).
  // Only the upstream SENDS are scoped; a single host's loss seeded only ITS channels, so this
  // does not re-SSUBSCRIBE every channel on every other host.
  std::vector<std::string> affected(pending_resubscribe_channels_.begin(),
                                    pending_resubscribe_channels_.end());

  bool success = true;
  // reissueSsubscribe re-resolves each channel's CURRENT owner and stamps a fresh generation, so a
  // stale owner recorded at signal time (a since-departed host) is replaced by the live one; it
  // also re-inserts the channel into pending_resubscribe_channels_ so a still-failing channel keeps
  // its generation-timeout retry + backoff protection. Entries leave the set when a
  // current-attempt ack lands (onPushMessage) or the channel is fully unsubscribed/removed; the
  // backoff resets only when it empties.
  for (const auto& channel : affected) {
    if (!subscriptions_.contains(channel)) {
      // Fully unsubscribed since the signal — nothing to re-send; drop the stale scope entry (and
      // reset the backoff / disarm the generation timer if it was the last outstanding channel).
      forgetPendingResubscribe(channel);
      continue;
    }
    ENVOY_LOG(debug, "redis: re-ssubscribing channel '{}'", channel);
    if (!reissueSsubscribe(channel)) {
      success = false;
    }
  }

  if (success) {
    // A successful *send* does not reset the backoff. SSUBSCRIBE is fire-and-forget, so the
    // upstream can still reject it asynchronously with -ERR (Redis <7 has no SSUBSCRIBE, or an
    // ACL/CROSSSLOT denial), which closes the subscription connection and loops back here. The
    // backoff is reset only when the upstream actually acks the SSUBSCRIBE (onPushMessage), so a
    // permanently-rejecting upstream escalates to the 30s cap instead of reconnecting every
    // ~100ms. The downstream SUBSCRIBE stays pending until an ack arrives; if none does within
    // kSubscribeAckTimeoutMs, handleSubscribeAckTimeout rolls the optimistic subscription back and
    // closes the subscriber connection, so a permanent rejection surfaces as a connection
    // close instead of hanging forever — operators should still run a Redis 7.0+ / compatible
    // upstream for steady-state sharded pub/sub.
    ENVOY_LOG(debug, "redis: re-subscribe SSUBSCRIBE(s) sent, awaiting upstream ack");
    armResubscribeGenerationTimer();
  } else {
    // A send failed (no healthy host / conn-pool hiccup). The failed channels stay in
    // pending_resubscribe_channels_ (reissueSsubscribe enrolled them before the send), so just
    // re-arm the escalating backoff to retry exactly them. Do NOT fall back to a
    // whole-registry re-mark: that re-marks and re-subscribes every OTHER host's healthy channels
    // too, and — now that owners are kept — would leave the successfully-rerouted channels'
    // in-flight acks intact but pointlessly re-send them, a re-subscribe storm on a single
    // channel's failure.
    ENVOY_LOG(warn, "redis: re-subscribe send failed, will retry affected channels with backoff");
    scheduleResubscribe();
  }
}

void SubscriptionRegistry::handleResubscribeGenerationTimeout() {
  if (pending_resubscribe_channels_.empty()) {
    return; // every re-sent channel acked (defensive; the timer is disabled on completion).
  }
  ENVOY_LOG(warn,
            "redis: {} re-subscribed channel(s) did not ack within the timeout; re-resolving on "
            "backoff",
            pending_resubscribe_channels_.size());
  // Re-resolve ONLY the still-unacked channels of this generation, not every channel:
  // they are exactly the ones still in pending_resubscribe_channels_ (a fully-acked generation
  // empties the set and disables this timer). doResubscribe re-issues that set — reissueSsubscribe
  // re-resolves each channel's CURRENT owner and stamps a fresh generation, so an owner that went
  // stale is replaced WITHOUT clearing it here (the owner must stay live through the backoff
  // window so a concurrent unsubscribe still sends its SUNSUBSCRIBE, a dedup subscribe still
  // defers, and the host's connection is not prematurely retired). The scheduled doResubscribe
  // re-sends exactly these on the still-escalating backoff and re-arms this timer — a bounded retry
  // loop to the 30s cap.
  scheduleResubscribe();
}

bool SubscriptionRegistry::reissueSsubscribe(
    const std::string& channel, const Upstream::HostConstSharedPtr& pre_resolved_host) {
  // Enroll in the current generation unconditionally: a channel whose send FAILS never acks, so it
  // keeps this generation incomplete and blocks a sibling's ack from resetting the backoff /
  // disarming the generation timer while it is still failing.
  pending_resubscribe_channels_.insert(channel);

  // Placement: choose the target host, resolving the slot AT MOST once per reissue.
  Upstream::HostConstSharedPtr target = pre_resolved_host;
  if (target == nullptr) {
    // No caller-provided placement: onClusterTopologyChange hands its dry-resolved host in
    // ``pre_resolved_host``; a null here is the connection-loss (doResubscribe) path, which decides
    // placement now. A retry is NOT a move: if the RECORD is still valid, re-target the recorded
    // home; only a channel with no valid record is re-placed.
    auto it = channel_hosts_.find(channel);
    Upstream::HostConstSharedPtr resolved;
    // This is the FAILURE-driven re-issue path (lost connection / failed send), so validity is
    // checked health-aware: a still-valid but unhealthy owner is re-placed onto a
    // healthy sibling rather than retried forever (topology reroutes stay health-agnostic).
    if (it != channel_hosts_.end() &&
        recordedOwnerValid(channel, it->second.host, resolved, ValidityMode::FailureDriven)) {
      target = it->second.host; // record valid -> re-target the recorded home (retry, not move)
    } else {
      // No valid record: re-place. recordedOwnerValid resolves the new owner of an INVALID record
      // and hands it back via ``resolved`` so this path does not resolve a second time:
      // the MASTER policy always does so; replica-capable policies do when an owner ESCAPED an
      // unhealthy home (the candidates were already computed for the validity check). ``resolved``
      // stays null only when the owner LEFT the shard entirely or the channel has no record at all,
      // so resolve fresh for those.
      target = (it != channel_hosts_.end() && resolved != nullptr) ? resolved
                                                                   : resolvePlacement(channel);
      // if a LIVE old owner is being left for a DIFFERENT host, that old host still carries
      // the upstream shard subscription. recordSsubscribeAttempt below would silently drop its last
      // owner reference (its internal forgetChannelHost) WITHOUT the SUNSUBSCRIBE — leaking the old
      // subscription (duplicate delivery + a forever-idle connection on a standalone / ring-hash
      // upstream with no Redis migration-invalidation). Pair it here exactly as the topology
      // reroute does (forget + courtesy SUNSUBSCRIBE, find-only so a dead old connection is a
      // harmless no-op).
      if (it != channel_hosts_.end() && target != nullptr && target != it->second.host) {
        forgetOwnerAndSunsubscribe(channel);
      }
    }
  }
  if (target == nullptr) {
    // No host available for the slot (no cluster / all hosts down). The channel stays enrolled in
    // pending_resubscribe_channels_, so the escalating backoff retries it.
    return false;
  }
  if (!upstream_callbacks_.sendUpstreamSsubscribeToHost(channel, *this, target)) {
    return false;
  }
  // Record the target host AND a fresh generation as the channel's current attempt: onPushMessage
  // correlates the SSUBSCRIBE ack against both so an ack from a superseded host OR a
  // superseded same-host attempt (A -> B -> A) neither completes a pending downstream subscribe nor
  // advances this generation.
  recordSsubscribeAttempt(channel, target);
  return true;
}

void SubscriptionRegistry::armResubscribeGenerationTimer() {
  // A FRESH subscribe has a per-bucket kSubscribeAckTimeoutMs (registerPendingSubscribeAck), but a
  // re-issued SSUBSCRIBE for an already-active channel — from a dropped connection (doResubscribe)
  // OR a slot move (onClusterTopologyChange) — has no downstream bucket, so a silently-lost ack
  // would stall the channel forever with no retry. If any enrolled channel is still unacked when
  // this elapses, handleResubscribeGenerationTimeout re-resolves them on the (escalating) backoff.
  // Timer is created lazily on first need.
  if (!pending_resubscribe_channels_.empty()) {
    if (resubscribe_generation_timer_ == nullptr) {
      resubscribe_generation_timer_ =
          dispatcher_.createTimer([this]() { handleResubscribeGenerationTimeout(); });
    }
    // Coalesce like the pool backoff timer (resubscribeTimerPending): keep an already-running
    // generation timer's deadline rather than pushing it out to a fresh full window on every
    // reissue batch. Otherwise a channel that keeps re-issuing (topology / backoff churn) would
    // defer its ack-timeout indefinitely and never retry. The timer re-arms after it fires
    // (handleResubscribeGenerationTimeout leaves it disabled, then re-enrolls), so escalation is
    // preserved.
    if (!resubscribe_generation_timer_->enabled()) {
      resubscribe_generation_timer_->enableTimer(subscribe_ack_timeout_);
    }
  }
}

void SubscriptionRegistry::dropHost(const Upstream::HostConstSharedPtr& host) {
  // The host is gone; none of its outstanding control commands (SSUBSCRIBE/SUNSUBSCRIBE) will ack
  // or error, so drop the whole per-host control FIFO — the single ledger.
  control_ledger_.clear(host);
  // Seed this host's channels into the explicit re-subscribe retry scope BEFORE dropping their
  // owner, so a new SUBSCRIBE for one of them in the (short) window before the posted
  // onClusterTopologyChange re-routes it is deferred + ack-timeout-protected by subscribe()'s dedup
  // gate rather than handed a premature success.
  markHostChannelsForResubscribe(host);
  // Then owner-less every channel this host served. The host is REMOVED, so keeping the owner is
  // pointless (a SUNSUBSCRIBE to it is a find-only no-op now) and would mislead
  // onClusterTopologyChange, which keys its "did the owner move?" decision off the CURRENT owner
  // and must see the stale removed host gone. The channels remain subscribed (in subscriptions_)
  // and are re-routed to their new owner by the posted onClusterTopologyChange (which re-enrolls
  // them in the retry scope and records the new owner).
  forgetHostChannels(host);
}

void SubscriptionRegistry::onClusterTopologyChange() {
  bool any_failed = false;
  bool any_reissued = false;
  // Ordering trade-off: ``SUNSUBSCRIBE`` to the old host is sent immediately followed by
  // ``SSUBSCRIBE`` to the new host, without waiting for the unsubscribe ack. The channel's recorded
  // owner is re-pointed to the new host as part of the SSUBSCRIBE (recordSsubscribeAttempt), and
  // the smessage fan-out's source-host check (see onPushMessage) then DROPS any message still
  // arriving from the OLD host. So a slot move is now a brief best-effort GAP — roughly one round
  // trip, from the moment the owner flips to the new host until that host's SSUBSCRIBE lands and it
  // begins delivering — rather than the former brief DUPLICATE window (both hosts delivering). We
  // deliberately choose "no duplicates, brief gap" over "no gap, brief duplicate": the same
  // source-host check is what stops a leaked/stale old owner (e.g. a refused SUNSUBSCRIBE) from
  // duplicate-delivering for the life of its connection, so making the normal reroute
  // duplicate-free keeps ONE rule for both. Pub/sub is best-effort and Redis does not promise
  // no-loss / no-duplicate across slot migrations, so a sub-RTT gap is within contract. Reviewers
  // asking about the former "continuity over duplicates" choice should see this note.
  for (const auto& [channel, _] : subscriptions_) {
    auto it = channel_hosts_.find(channel);
    Upstream::HostConstSharedPtr resolved;
    // Record still valid — the recorded home matches the current slot owner, OR the resolve is a
    // TRANSIENT null (slot-migration window / momentary primary absence — a genuine ownership loss
    // arrives separately as Redis's unsolicited SUNSUBSCRIBE). Leave it untouched: re-issuing
    // SUNSUBSCRIBE+SSUBSCRIBE on an unmoved channel wastes upstream control traffic AND opens a
    // needless message gap / duplicate window. A slot-only rebalance moves only some slots, so
    // most active channels take this skip. ``recordedOwnerValid`` returns the resolution via
    // ``resolved`` so the re-place branch below does not resolve a second time.
    if (it != channel_hosts_.end() &&
        recordedOwnerValid(channel, it->second.host, resolved, ValidityMode::TopologyEvent)) {
      continue;
    }
    // Record invalid (slot moved to a DIFFERENT known host) or owner-less: (re)place the channel. A
    // LIVE old owner is dropped + sent SUNSUBSCRIBE BEFORE reissue so any subscription-mode cleanup
    // along the send path observes the up-to-date map and tears down push_callbacks_ on the old
    // client. recordedOwnerValid already resolved the new owner for an invalid record and hands it
    // back via ``resolved`` (reused as reissueSsubscribe's pre_resolved_host). An owner-less
    // channel has no old owner and no pre-resolved host, so reissueSsubscribe resolves it below — a
    // single resolve, rather than resolving here and again there when the first attempt returns
    // null.
    if (it != channel_hosts_.end()) {
      forgetOwnerAndSunsubscribe(channel);
    }
    any_reissued = true;
    // Reissue via the shared helper so the rerouted channel is enrolled in the resubscribe
    // generation (and records its new expected ack owner) exactly like the connection-loss path — a
    // silently-lost SSUBSCRIBE ack is then retried. Hand off the already-resolved host so
    // the send does not re-resolve.
    if (!reissueSsubscribe(channel, resolved)) {
      ENVOY_LOG(warn, "redis: topology change: failed to re-route ssubscribe for '{}'", channel);
      any_failed = true;
    }
  }
  // Arm the generation ack timeout ONLY for a pass that actually rerouted a channel. The
  // rerouted channels clear from pending_resubscribe_channels_ as their new owners ack
  // (host-correlated in onPushMessage), and if any is still unacked when it fires,
  // handleResubscribeGenerationTimeout re-resolves it on backoff. When this pass reissued NOTHING
  // (every active channel's owner was unchanged — the common slot-only rebalance), the set may
  // still hold channels enrolled by an UNRELATED prior signal that a pool backoff cycle already
  // owns; arming here would (re)start a generation clock over those channels and, on timeout, call
  // scheduleResubscribe — needless churn that the coalesce now absorbs but that should not be
  // triggered in the first place. So only arm when this pass owns channels in the generation.
  if (any_reissued) {
    armResubscribeGenerationTimer();
  }
  // On a planned host removal the pool suppresses its connection-loss resubscribe to avoid a double
  // resubscribe, so this sharded re-route is the sole re-router for channels whose slot owner
  // just changed. If any re-route SEND failed, retry exactly those on backoff: reissueSsubscribe
  // enrolled each failed channel in pending_resubscribe_channels_ before its send, so a plain
  // scheduleResubscribe re-sends them. Do NOT fall back to a whole-registry re-mark: it
  // would re-mark every OTHER channel too — including the ones that just rerouted SUCCESSFULLY —
  // pointlessly re-SSUBSCRIBE-ing them and staling their in-flight acks.
  if (any_failed) {
    scheduleResubscribe();
  }
}

void SubscriptionRegistry::deliverFrameToSubscribers(
    const absl::flat_hash_set<DownstreamSubscriberPtr>& subscribers,
    const Common::Redis::RespValue& frame) {
  // Single (or zero) subscriber — the common case on a per-shard channel. Copy the one shared_ptr
  // to the stack and deliver directly: pre-encoding buys nothing, and the stack copy already gives
  // the reentrancy safety the fan-out snapshot below provides (deliver() writes downstream and can
  // synchronously drive a disconnect -> ProxyFilter::onEvent -> removeSubscriber, which mutates
  // ``subscribers``; the stack copy keeps this subscriber alive and we never touch ``subscribers``
  // again). Avoids a per-message heap vector allocation on the delivery hot path.
  if (subscribers.size() <= 1) {
    if (subscribers.empty()) {
      return;
    }
    const DownstreamSubscriberPtr subscriber = *subscribers.begin();
    subscriber->deliverMessage(frame);
    return;
  }
  // Real fan-out (>= 2 subscribers): snapshot before delivering, because deliver() can
  // synchronously mutate/rehash ``subscribers`` mid-iteration (iterator UAF). Same guard clear()
  // uses. The frame is identical for every subscriber, so encode it once (into the reused member
  // buffer/encoder) and share those bytes with each — turns N tree-walks + N length-formats
  // into 1, and N full payload copies into 1 copy (the encode->string below) plus a small
  // per-subscriber zero-copy fragment. InlinedVector keeps the common small fan-out off the heap.
  // Snapshot into the reused member vector: a > 8-subscriber channel keeps its heap buffer
  // across messages instead of allocating one per message. Cleared at the end so it never pins
  // subscribers between deliveries. Same non-nesting invariant as fanout_encoder_/fanout_buffer_.
  fanout_targets_.assign(subscribers.begin(), subscribers.end());
  fanout_encoder_.encode(frame, fanout_buffer_);
  // One serializing copy of the encode into a reference-counted string that every subscriber's
  // fragment references; freed when the last fragment drains. Drain the scratch buffer now.
  auto shared_bytes = std::make_shared<const std::string>(fanout_buffer_.toString());
  fanout_buffer_.drain(fanout_buffer_.length());
  for (const auto& subscriber : fanout_targets_) {
    subscriber->deliverSharedFrame(shared_bytes);
  }
  fanout_targets_.clear();
}

void SubscriptionRegistry::onPushMessage(Common::Redis::RespValuePtr&& value,
                                         const Upstream::HostConstSharedPtr& host) {
  if (!value || value->type() != Common::Redis::RespType::Push) {
    ENVOY_LOG(debug, "redis: received non-Push type message in onPushMessage, ignoring");
    return;
  }

  const auto& array = value->asArray();
  if (array.empty()) {
    ENVOY_LOG(debug, "redis: received empty push message, ignoring");
    return;
  }

  if (!isRespString(array[0])) {
    ENVOY_LOG(debug, "redis: push message type element is not a string, ignoring");
    return;
  }
  const std::string& msg_type = array[0].asString();

  if (msg_type == "smessage") {
    // Per-shard pub/sub message: [type, channel, data]. This is the only message push the upstream
    // sends now that every subscription is issued as ``SSUBSCRIBE`` (broadcast ``SUBSCRIBE`` was
    // removed); it is normalized to ``message`` below for the client.
    if (array.size() < 3) {
      ENVOY_LOG(debug, "redis: malformed 'smessage' push (expected 3 elements, got {})",
                array.size());
      return;
    }
    if (!isRespString(array[1])) {
      ENVOY_LOG(debug, "redis: malformed 'smessage' push: channel is not a string, ignoring");
      return;
    }
    const std::string& channel = array[1].asString();
    auto it = subscriptions_.find(channel);
    if (it == subscriptions_.end()) {
      return;
    }
    // Source-host verification: a channel's messages must come from its CURRENT owner. After a
    // re-route A -> B — or any window where the old owner A did not actually drop its shard
    // subscription (its SUNSUBSCRIBE was refused/failed, or a late race) — A can keep pushing
    // ``smessage`` for a channel now owned by B. Delivering both A's and B's copies would duplicate
    // every message to the client for as long as A lingers subscribed. Drop a message from a host
    // that is not the channel's recorded owner. When the channel is owner-less (a transient
    // mid-re-resolve window with no recorded owner) we cannot attribute a source, so deliver —
    // matching the prior behavior for that window. ``host`` is null only in registry unit-test
    // injection; skip the check there.
    if (host != nullptr) {
      auto owner_it = channel_hosts_.find(channel);
      if (owner_it != channel_hosts_.end() && owner_it->second.host != host) {
        ENVOY_LOG(debug,
                  "redis: dropping 'smessage' for '{}' from a non-owning host (stale/leaked shard "
                  "subscription); the current owner delivers its own copy",
                  channel);
        return;
      }
    }
    // Normalize ``smessage`` to ``message`` for downstream delivery. The splitter only ever
    // exposes ``SUBSCRIBE`` to clients (sharded routing is internal), so clients expect the
    // traditional ``message`` push type — they never asked for sharded pub/sub and would not
    // know how to frame ``smessage``. ``value`` is uniquely owned here (RespValuePtr&&), so
    // rewrite the type element in place and deliver the same frame — no new array allocation and
    // no channel/payload copy on this per-message hot path. Force BulkString so the emitted verb
    // is byte-identical regardless of whether upstream framed ``smessage`` as a simple or bulk
    // string.
    value->asArray()[0].type(Common::Redis::RespType::BulkString);
    value->asArray()[0].asString() = "message";
    deliverFrameToSubscribers(it->second, *value);
  } else if (msg_type == "ssubscribe") {
    // Only ``ssubscribe`` acks arrive upstream: every subscription is issued as ``SSUBSCRIBE``,
    // so a plain ``subscribe`` ack is unreachable.
    if (array.size() < 2 || !isRespString(array[1])) {
      ENVOY_LOG(debug, "redis: malformed upstream '{}' ack, dropping", msg_type);
      return;
    }
    // Bind by const& like the smessage / sunsubscribe siblings: every use below (consumeAck,
    // ssubscribeAckIsCurrent, forgetPendingResubscribe, deliverPendingSubscribeAck) reads registry
    // maps only and never moves or mutates ``value``, so array[1] stays valid — no per-ack copy.
    const std::string& target = array[1].asString();
    // Drop this ack's entry from the host's outstanding-control FIFO FIRST, and learn WHICH attempt
    // (generation) it acked: acks return in per-connection FIFO order, so the oldest outstanding
    // (ssubscribe, target) send on this host is the one being acked. (Also keeps a later error from
    // attributing itself to the wrong already-acked command.)
    const std::optional<uint64_t> acked_generation =
        control_ledger_.consumeAck(host, "ssubscribe", target);
    // An ack completes/advances the channel only if it is for the CURRENT attempt — same owning
    // host AND same generation. This one predicate rejects an owner-less-gap ack, a
    // wrong-host ack, and a SAME-HOST stale ack from a superseded attempt (A -> B -> A)
    // whose host alone would pass. A stale ack must neither advance the resubscribe generation /
    // disarm its timer nor complete the pending downstream subscribe.
    const bool ack_is_current = ssubscribeAckIsCurrent(target, host, acked_generation);
    // Reset the re-subscribe backoff only when the CURRENT resubscribe generation has fully acked
    // Resetting on any single channel's ack would let a partial failure — some channels ack
    // while another keeps -ERR-ing and closing the connection — reset the backoff every cycle and
    // hot-loop at the floor. doResubscribe seeds pending_resubscribe_channels_ with the channels it
    // re-sent; the backoff resets only when the last of them acks. (Send-success alone is not proof
    // of acceptance — see doResubscribe — so only a genuine current-attempt ack clears an entry.) A
    // stale ack (wrong host / superseded attempt) must NOT clear the scope, hence the
    // ack_is_current gate; forgetPendingResubscribe does the erase + on-empty backoff-reset /
    // timer-disarm.
    if (ack_is_current) {
      forgetPendingResubscribe(target);
    }
    deliverPendingSubscribeAck(target, ack_is_current);
  } else if (msg_type == "sunsubscribe") {
    // An upstream ``sunsubscribe`` push arrives in TWO cases that share a wire shape:
    //  1. The advisory ack to a SUNSUBSCRIBE the registry itself sent (a channel's last subscriber
    //  left, or a topology re-route). Those live as the (sunsubscribe, channel) entry the send
    //  recorded on the host's outstanding-control FIFO, so consume one here and leave state
    //  alone.
    //  2. UNSOLICITED — this node lost ownership of the channel's slot (Redis slot migration:
    //  pubsubShardUnsubscribeAllChannelsInSlot -> addReplyPubsubUnsubscribed) and dropped our
    //  shard subscription while the channel is STILL active here. Forget the now-stale
    //  channel->host mapping so it is re-resolved; leave ``subscriptions_`` intact.
    // The expected-ack check MUST come first: a topology change / rapid re-subscribe can leave a
    // channel active AND our own SUNSUBSCRIBE ack still in flight — without it we would mistake our
    // ack for an invalidation and wrongly forget the freshly-set host. We do NOT re-subscribe here:
    // the local slot map may still point at the old owner, so re-SSUBSCRIBE-ing would draw another
    // unsolicited sunsubscribe (a loop); the slot-map refresh -> onClusterTopologyChange re-routes
    // the whole subscription set to current owners.
    if (array.size() >= 2 && isRespString(array[1])) {
      const std::string& channel = array[1].asString();
      // ADVISORY ack vs UNSOLICITED invalidation is decided by the host's outstanding-control FIFO
      // alone: if its head is the ``sunsubscribe`` we sent for this channel, this is our
      // own ack — consume it and leave the mapping. Every ssubscribe/sunsubscribe ack AND every
      // control error pops the FIFO head (acks return in per-connection FIFO order), so the head is
      // exactly the oldest command still awaiting a reply — a head match therefore means an
      // outstanding expected SUNSUBSCRIBE ack. ``host`` is always the known source host, so
      // the FIFO lookup is exact.
      if (control_ledger_.consumeAck(host, "sunsubscribe", channel).has_value()) {
        ENVOY_LOG(debug, "redis: upstream 'sunsubscribe' ack for '{}' consumed (advisory)",
                  channel);
        return;
      }
      auto sub_it = subscriptions_.find(channel);
      if (sub_it != subscriptions_.end() && !sub_it->second.empty()) {
        // Only the channel's CURRENT owner losing the slot should invalidate the mapping. Once the
        // channel has re-resolved A -> B, a late or duplicate SUNSUBSCRIBE still arriving from the
        // OLD owner A (Redis can emit both an unsolicited invalidation and our own ack around a
        // migration) must NOT erase B — that would strand the live subscription until the next
        // topology trigger. ``host`` is the known source host; compare it to the current
        // owner.
        auto host_it = channel_hosts_.find(channel);
        if (host_it != channel_hosts_.end() && host_it->second.host != host) {
          ENVOY_LOG(debug, "redis: stale 'sunsubscribe' for '{}' from a non-owning host, ignoring",
                    channel);
          // the channel already re-resolved off this SOURCE host, so this late/duplicate
          // unsolicited SUNSUBSCRIBE proves the source no longer owns it. If it was the source's
          // last channel, its dedicated subscription connection is now idle — retire it (every
          // other last-channel path does); the helper clears the control ledger on the pool's real
          // retire.
          retireHostConnectionIfIdle(host);
          return;
        }
        ENVOY_LOG(debug,
                  "redis: upstream unsolicited 'sunsubscribe' for active channel '{}' (slot moved "
                  "off this owner); forgetting stale host and re-resolving",
                  channel);
        forgetChannelHost(channel);
        // If that was this owner's last channel, its dedicated subscription connection is now idle.
        // Unlike a client UNSUBSCRIBE (which sends a SUNSUBSCRIBE whose path retires an emptied
        // connection), the unsolicited case sends nothing, so ask the pool to retire it now rather
        // than let it linger until an unrelated event closes it. Do this BEFORE
        // scheduling the re-resolve: the channel re-subscribes to its NEW owner, not this departed
        // one.
        //
        // Retire the now-idle connection (the helper clears the host's control ledger on the pool's
        // real retire — a retired connection never acks its outstanding control commands,
        // and a stale FIFO head would black out a future subscribe routed back here). A host that
        // still owns channels keeps its connection and its legitimately-outstanding acks.
        retireHostConnectionIfIdle(host);
        // The slot moved off this owner, so the local slot map is stale — a plain re-SSUBSCRIBE
        // would draw the same invalidation. Refresh the cluster topology (throttled) AND schedule a
        // backoff re-resolve, so the channel re-subscribes to its NEW owner promptly instead of
        // sitting in subscriptions_ with no upstream subscription until an unrelated topology event
        // fires. forgetChannelHost cleared the mapping because the slot GENUINELY moved off this
        // owner, so ADD the channel to the explicit re-subscribe signal set — doResubscribe
        // re-resolves exactly that set. This is the one owner-less-at-signal case that is
        // correct: the channel really is not subscribed anywhere upstream now, so there is nothing
        // to leak and no SUNSUBSCRIBE to send. (Re-subscribe must NOT be issued synchronously here:
        // the slot map may still point at the old owner, so it would loop; the backoff gives the
        // refresh time to land.) Only THIS channel's slot moved, so re-resolve just it — the host's
        // other channels keep their mapping.
        pending_resubscribe_channels_.insert(channel);
        upstream_callbacks_.requestTopologyRefresh();
        scheduleResubscribe();
        return;
      }
    }
    ENVOY_LOG(debug, "redis: upstream 'sunsubscribe' advisory ack, dropping");
  } else {
    // Any other push type is unexpected and simply dropped. The proxy only ever issues the sharded
    // ``ssubscribe`` / ``sunsubscribe`` verbs upstream (both handled above), so Redis never acks a
    // classic ``subscribe`` / ``unsubscribe`` here.
    ENVOY_LOG(debug, "redis: unexpected upstream push message type '{}', dropping", msg_type);
  }
}

void SubscriptionRegistry::registerPendingSubscribeAck(
    const std::string& target, const DownstreamSubscriberPtr& subscriber, uint64_t snapshot_count,
    bool newly_added, const SubscriptionAckTargetWeakPtr& ack_target) {
  appendPendingSubscribeAck(pending_subscribe_acks_[target], target, subscriber, snapshot_count,
                            newly_added, ack_target);
}

void SubscriptionRegistry::appendPendingSubscribeAck(
    PendingBucket& bucket, const std::string& target, const DownstreamSubscriberPtr& subscriber,
    uint64_t snapshot_count, bool newly_added, const SubscriptionAckTargetWeakPtr& ack_target) {
  const bool new_bucket = bucket.entries_.empty();
  bucket.entries_.push_back(
      {std::weak_ptr<DownstreamSubscriber>(subscriber), snapshot_count, newly_added, ack_target});
  // Schedule the subscribe-ack timeout. If the upstream never acks this subscribe, the timeout
  // fails it downstream and rolls it back instead of hanging the SUBSCRIBE forever. A subscriber
  // that JOINS a still-pending bucket waits on the same upstream ack, so it reuses the bucket's
  // existing schedule entry — only a FRESH bucket needs a new one. All buckets share ONE timer
  // the deadline is constant, so a new entry's deadline is always >= the last, keeping
  // subscribe_ack_schedule_ ordered.
  if (!new_bucket) {
    return;
  }
  // A FRESH bucket gets a deadline; store the scheduler's token so its live-predicate can tell this
  // registration from a later re-subscribe under a new token. The scheduler arms the shared
  // timer on the empty -> non-empty edge.
  bucket.schedule_seq_ = ack_scheduler_.schedule(target);
}

void SubscriptionRegistry::failPendingSubscribers(const std::string& target,
                                                  const std::string& error_message) {
  auto it = pending_subscribe_acks_.find(target);
  if (it == pending_subscribe_acks_.end()) {
    // No pending downstream bucket for this channel: nothing to fail. This is the normal case for
    // an error to a re-issued SSUBSCRIBE of an already-ACTIVE channel (no pending bucket — the
    // caller re-resolves it rather than failing downstream), and a defensive no-op when the
    // ack-timeout drained a schedule entry whose bucket was already acked (ack_scheduler_ only
    // fires this on a token match, but the guard keeps every caller safe).
    return;
  }
  // Move the bucket out and erase it BEFORE failing subscribers: the unsubscribe() below then finds
  // no pending entry to re-scan (its scrubPendingBucket becomes a no-op), so the whole fan-in
  // drains in one O(N) pass. Erasing the bucket leaves its (now-stale) ack_scheduler_ deadline
  // entry behind; the scheduler prunes it lazily on a token miss, so teardown stays O(1).
  auto bucket = std::move(it->second);
  pending_subscribe_acks_.erase(it);
  ENVOY_LOG(warn, "redis: failing {} pending subscriber(s) for '{}': {}", bucket.entries_.size(),
            target, error_message);
  // Fail every subscriber that was waiting on this channel's single upstream ack — but count +
  // roll back ONCE per (subscriber, channel), not once per bucket entry. A duplicate
  // pipelined SUBSCRIBE (``SUBSCRIBE ch ch``) parks TWO entries for the SAME subscriber on this one
  // channel, yet the channel was added to the pubsub_active_subscriptions gauge only once
  // (addChannel deduplicates) and is rolled back once — so recording pubsub_subscribe_ack_error per
  // entry would drift the documented identity
  // ``active = pubsub_subscribe_total − pubsub_unsubscribe_total − pubsub_subscribe_ack_error`` by
  // −1 per duplicate (pubsub_subscribe_total is the NET subscription delta, +1). Gate on the
  // subscriber still holding the channel: the FIRST entry counts + rolls back + closes; a duplicate
  // second entry (or an entry whose subscriber an earlier fail already tore down) finds the channel
  // gone and is skipped.
  for (auto& entry : bucket.entries_) {
    auto subscriber = entry.subscriber.lock();
    if (subscriber == nullptr) {
      continue; // disconnected before the failure
    }
    if (!entry.newly_added) {
      // A DUPLICATE SUBSCRIBE entry from a subscriber that already held the channel: its live
      // subscription was established by an EARLIER call (or another still-pending entry this loop
      // handles), so it is NOT this failed attempt's to roll back or close. Leave it
      // subscribed; the channel's re-resolution in the caller restores its upstream. But a
      // duplicate SUBSCRIBE still deserves an ack — real Redis acks it unconditionally, and the
      // SUCCESS path delivers one. So if the subscription SURVIVES this failure (the
      // subscriber still holds the channel — i.e. a same-subscriber fresh entry did not just roll
      // it back and close), emit the echo-ack now rather than dropping the duplicate's SUBSCRIBE
      // unanswered. (A fresh
      // ``SUBSCRIBE ch ch`` whose first entry closed the subscriber leaves ``contains`` false here,
      // so the duplicate is correctly moot.)
      if (subscriber->subscribedChannels().contains(target)) {
        // Route the surviving duplicate's success ack to its own waiting request (FIFO), or fall
        // back to out-of-band deliver() for a direct-registry caller / a request already torn down.
        if (auto target_req = entry.ack_target.lock()) {
          target_req->onSubscribeAck(target, entry.snapshot_count);
        } else {
          subscriber->deliver(makeSubscriptionAck("subscribe", &target, entry.snapshot_count));
        }
      }
      continue;
    }
    if (!subscriber->subscribedChannels().contains(target)) {
      continue; // already failed for this subscriber+channel by an earlier (duplicate) entry
    }
    subscriber->recordSubscribeAckError();
    // Roll back this subscriber's failed channel BEFORE closing. unsubscribe() removes the channel
    // through removeChannel, which decrements the pubsub_active_subscriptions gauge — so the
    // optimistic increment made when the channel was added is undone here, with no separate
    // rollback accounting. The gauge drops exactly once (the guard above already skipped
    // duplicates).
    unsubscribe(absl::MakeConstSpan(&target, 1), subscriber);
    // close the subscriber's connection instead of writing a bare out-of-band ``-ERR``. An
    // unsolicited Error frame on a RESP3 connection has no reply to attach to, so a pipelining
    // client attributes it to the wrong (earlier, still-pending) command — scrambling
    // its request/response matching. Closing is protocol-clean and consistent with slow-subscriber
    // eviction; the resulting disconnect cleans up the subscriber's remaining subscriptions. (The
    // ``bucket`` was moved out and erased above, so this close-driven removeSubscriber cannot
    // mutate the entries we are iterating.)
    subscriber->close();
  }
}

void SubscriptionRegistry::handleSubscribeAckTimeout(std::string target) {
  failPendingSubscribers(target, "ERR subscribe to '" +
                                     Common::Redis::sanitizeControlBytes(target) + "' timed out");
}

void SubscriptionRegistry::deliverPendingSubscribeAck(const std::string& target,
                                                      bool ack_is_current) {
  auto it = pending_subscribe_acks_.find(target);
  if (it == pending_subscribe_acks_.end()) {
    // No pending entry — extra/late upstream ack (e.g. after local rollback or replay
    // race). Safe to drop.
    return;
  }
  // Only the CURRENT attempt's ack completes the pending downstream subscribe (see
  // ssubscribeAckIsCurrent, computed by the caller from host + control-FIFO generation). A stale
  // ack — owner-less gap, wrong host, or a superseded same-host attempt A -> B -> A —
  // must leave the bucket parked for the current attempt's ack rather than hand the
  // client a premature
  // ``subscribe`` success while no live upstream subscription exists on the current owner.
  if (!ack_is_current) {
    ENVOY_LOG(debug, "redis: ignoring stale ssubscribe ack for '{}' (not the current attempt)",
              target);
    return;
  }
  // Move the whole bucket out and erase it — the upstream ack arrived, so these subscribes are no
  // longer pending.
  auto bucket = std::move(it->second);
  pending_subscribe_acks_.erase(it);
  // Eager-prune the shared-timer schedule and re-arm for the earliest live deadline: if this acked
  // channel was the earliest outstanding one, its now-dead entry is dropped immediately instead of
  // lingering until its deadline and waking the timer to a token-miss no-op.
  ack_scheduler_.pruneAndRearm();
  // Build the ack skeleton ``["subscribe", target, count]`` once: verb and target are identical for
  // every entry in this bucket (same channel), so only the trailing per-subscriber Integer count
  // varies between subscribers. The downstream verb is always the literal ``subscribe`` (not the
  // upstream verb ``ssubscribe``): the client only ever issued ``SUBSCRIBE`` and the splitter
  // rewrote it to ``SSUBSCRIBE`` internally, so cluster sharding stays invisible.
  // Fallback ack skeleton, used only for entries with no live ack_target (see below). Built once:
  // verb and target are identical for every entry (same channel), so only the trailing
  // per-subscriber Integer count varies.
  Common::Redis::RespValue ack =
      makeSubscriptionAck("subscribe", &target, /*subscription_count=*/0);
  // Drain-first is already done (bucket moved out above), so a re-entrant teardown a delivery
  // drives (onSubscribeAck -> respond() -> a write that closes the connection) only expires the
  // LATER entries' weak refs, which the ``lock()`` guards below skip — it cannot corrupt this local
  // bucket or the erased map slot.
  for (auto& entry : bucket.entries_) {
    auto sub = entry.subscriber.lock();
    if (!sub) {
      continue; // disconnected before ack arrived
    }
    // Full-strict ordering: hand the confirming ack to the in-flight SUBSCRIBE request waiting on
    // it (its weak ack_target) so the request completes and flushes at its FIFO position, ordered
    // against any pipelined command replies. It builds the ack from the snapshot count — Redis's
    // "number of subscriptions the client has at THIS step" (upstream's echoed count is the
    // registry-wide distinct-channel count, wrong for a per-subscriber ack). A registration with no
    // live target — a direct registry caller (tests), or a request already torn down (connection
    // close) — falls back to the legacy out-of-band write: for a dead request the subscriber is
    // closing too, so deliver() is a safe no-op; a test exercises the same on-the-wire ack bytes.
    if (auto target_req = entry.ack_target.lock()) {
      target_req->onSubscribeAck(target, entry.snapshot_count);
    } else {
      ack.asArray()[2].asInteger() = entry.snapshot_count;
      sub->deliver(ack);
    }
    // Count success ONCE per (subscriber, channel) — a pipelined ``SUBSCRIBE ch ch`` gets two acks
    // (per entry, matching real Redis) but only the newly_added entry established the subscription
    // (and bumped the active gauge once); a duplicate re-acked but established nothing, so it must
    // not re-count, symmetric with the error path's dedup gate. Gate on connectionOpen() too: an
    // earlier entry's delivery in this loop can trip the slow-subscriber watermark and close THIS
    // subscriber, in which case deliver()/onSubscribeAck just dropped the confirming ack at the
    // Open gate — so the success counter must not move for an ack never delivered (R8-13).
    if (entry.newly_added && sub->connectionOpen()) {
      sub->recordSubscribeAckSuccess();
    }
  }
}

void SubscriptionRegistry::dropPendingForSubscriber(const DownstreamSubscriberPtr& subscriber) {
  // Scrub this subscriber (and any expired-weak entries) from every pending-ack bucket on THIS
  // registry. There is no per-subscriber ``pending_ack_keys_`` mirror to consult: the pending
  // map is a per-thread transient bounded by the subscribe-ack timeout window (a channel leaves it
  // the moment its upstream ack lands or times out), so it is normally near-empty and this
  // whole-map sweep is cheap — cheaper in maintenance than the five-site mirror + cross-registry
  // ownership dance it replaces. Only this registry's buckets are visited; the subscriber's entries
  // in OTHER tracked registries are scrubbed by their own removeSubscriber pass. Snapshot the keys
  // first because scrubPendingBucket erases emptied buckets, which would invalidate a live
  // iterator.
  std::vector<std::string> keys;
  keys.reserve(pending_subscribe_acks_.size());
  for (const auto& [key, _] : pending_subscribe_acks_) {
    keys.push_back(key);
  }
  for (const auto& key : keys) {
    scrubPendingBucket(key, subscriber);
  }
}

void SubscriptionRegistry::scrubPendingBucket(const std::string& target,
                                              const DownstreamSubscriberPtr& subscriber) {
  auto it = pending_subscribe_acks_.find(target);
  if (it == pending_subscribe_acks_.end()) {
    return;
  }
  // Remove exactly this subscriber's entries — and any whose weak_ptr has expired — and erase the
  // bucket only when it empties; other subscribers on the same channel keep waiting.
  auto& entries = it->second.entries_;
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&subscriber](const PendingSubscribeAck& e) {
                                 auto sp = e.subscriber.lock();
                                 return !sp || sp == subscriber;
                               }),
                entries.end());
  if (entries.empty()) {
    pending_subscribe_acks_.erase(it);
  }
}

bool SubscriptionRegistry::collectPreservedSubscribeAcks(
    const std::string& target, const DownstreamSubscriberPtr& subscriber,
    std::vector<Common::Redis::RespValue>& out_acks) {
  auto it = pending_subscribe_acks_.find(target);
  if (it == pending_subscribe_acks_.end()) {
    return false; // not subscribing (already ACTIVE, or rolled back) — nothing to preserve.
  }
  // Redis-compatible SUBSCRIBE-then-UNSUBSCRIBE: the pending ``subscribe`` ack must still be
  // delivered (``subscribe ch <n>`` then ``unsubscribe ch <n-1>``). WHERE it goes depends on
  // whether a SUBSCRIBE request is still holding a FIFO slot for it:
  //   - A live ack_target (the common full-strict path) means a SUBSCRIBE request is parked in the
  //     response FIFO waiting on this ack. Its ack must COMPLETE that request at its OWN FIFO
  //     position, NOT ride out on this UNSUBSCRIBE response: doing the latter would leave the
  //     SUBSCRIBE request forever pending (the bucket that completes it is scrubbed below) and,
  //     being ahead in the FIFO, block this UNSUBSCRIBE reply behind it — a permanent stall. The
  //     completion is DEFERRED via dispatcher_.post so onSubscribeAck -> respond() cannot re-enter
  //     and close the connection while THIS UNSUBSCRIBE command is still on the stack (reentrancy/
  //     UAF). The weak target lapses harmlessly if the request is already gone when the post fires.
  //   - No live target (a direct-registry caller/test, or the SUBSCRIBE request already torn down):
  //     nothing to complete — preserve the ack into ``out_acks`` for the caller to flush, the
  //     pre-full-strict behavior.
  // One ack per entry the subscriber parked (a pipelined duplicate SUBSCRIBE has its own entry).
  // The downstream verb is always ``subscribe``; the count is the snapshot at subscribe-call time.
  // We do NOT touch the subscribe-ack outcome counters — this is a client-cancelled subscribe, not
  // an upstream ack resolution. Completing the ``subscribe`` here as SUCCESS is a DELIBERATE
  // Redis-compatible short-circuit, not a bug: the client pipelined SUBSCRIBE-then-UNSUBSCRIBE, and
  // real Redis — which resolves the SUBSCRIBE synchronously before it processes the UNSUBSCRIBE —
  // shows exactly this pair (``subscribe ch <n>`` then ``unsubscribe ch <n-1>``). A late upstream
  // SSUBSCRIBE ack OR error for this now-cancelled channel is thereafter ignored (its bucket is
  // scrubbed below); see the "UNSUBSCRIBE before the upstream confirms" note in docs redis.rst.
  // Two things this intentionally does NOT do, both correct:
  //   - It does not HOLD the subscribe ack for the real upstream outcome. Holding it would also
  //     hold this UNSUBSCRIBE ack behind it in the response FIFO (the SUBSCRIBE request is still
  //     parked), re-introducing the pipeline stall / ack-timeout connection-close that the
  //     FIFO-ordering Blocker fix removed — a regression for a subscription the client has already
  //     abandoned.
  //   - It does not record ``pubsub_subscribe_ack_error`` if that ignored late ack turns out to be
  //     an error. The teardown of this cancelled subscribe is already counted as an UNSUBSCRIBE
  //     (pubsub_unsubscribe_total), and the documented identity ``active = subscribe_total −
  //     unsubscribe_total − subscribe_ack_error`` reserves the error counter for rollbacks NOT
  //     otherwise counted as an unsubscribe. Adding it here would double count the removal and
  //     drift the gauge by −1.
  bool collected = false;
  for (auto& entry : it->second.entries_) {
    auto sp = entry.subscriber.lock();
    if (sp != subscriber) {
      continue;
    }
    if (entry.ack_target.lock() != nullptr) {
      SubscriptionAckTargetWeakPtr weak = entry.ack_target;
      const std::string channel(target);
      const uint64_t count = entry.snapshot_count;
      dispatcher_.post([weak, channel, count]() {
        if (auto target_req = weak.lock()) {
          target_req->onSubscribeAck(channel, count);
        }
      });
    } else {
      out_acks.push_back(
          makeSubscriptionAck("subscribe", &target, /*subscription_count=*/entry.snapshot_count));
    }
    collected = true;
  }
  if (collected) {
    // Drop exactly this subscriber's now-collected entries and release the bucket (and its timeout
    // timer) if it empties; other subscribers on the same channel stay pending.
    scrubPendingBucket(target, subscriber);
  }
  return collected;
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
