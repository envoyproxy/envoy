#include "source/extensions/filters/network/redis_proxy/subscription_registry.h"

#include <algorithm>
#include <string>
#include <vector>

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
  // Drain the gauge for any channels still owned at destruction (A-2). Normal
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
  // ~DownstreamSubscriber (A-2).
  if (connection_.state() == Network::Connection::State::Open) {
    connection_.close(Network::ConnectionCloseType::NoFlush);
  }
}

void DownstreamSubscriber::deliver(const Common::Redis::RespValue& message) {
  // A single out-of-band ack is appendAck (encode into the reused batch buffer) + flushAckBatch
  // (single write): the same encode-then-single-write path and no-count semantics (G10) the
  // multi-channel batch acks use — there is no separate deliverBatch primitive (§6). No yield
  // separates the two, so their identical Open checks coincide. These are out-of-band control acks,
  // NOT counted in pubsub_push_messages_delivered (message frames only).
  appendAck(message);
  flushAckBatch();
}

void DownstreamSubscriber::appendAck(const Common::Redis::RespValue& ack) {
  // Side-effect-free accumulate into the shared batch buffer (E-6): no write here, so the caller
  // can encode acks mid-loop and still defer their delivery to flushAckBatch after its terminal
  // respond(). Skip once the connection is gone — flushAckBatch drains whatever accumulated.
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
  // Reference the shared encode via a zero-copy fragment instead of copying the payload into this
  // subscriber's buffer (E-2): the whole fan-out then costs ONE buffer->string copy (in
  // deliverFrameToSubscribers) plus a small per-subscriber fragment, not N full payload copies. The
  // releasor captures ``bytes`` so the shared string outlives the fragment, and deletes the
  // heap-allocated fragment once the connection (or the FIFO park buffer it is moved into) drains
  // it — including on teardown, when the owning buffer's destructor drains its fragments.
  auto* fragment = new Buffer::BufferFragmentImpl(
      bytes->data(), bytes->size(),
      [bytes](const void*, size_t, const Buffer::BufferFragmentImpl* frag) { delete frag; });
  // Reuse the subscriber's member buffer (E-3, as deliverMessage does) instead of a per-delivery
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
    // empty for the next push — no explicit drain needed. Do NOT count here (D3): the frame may be
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

bool SubscriptionRegistry::addSubscription(const std::string& channel,
                                           const DownstreamSubscriberPtr& subscriber) {
  subscriber->addChannel(channel);
  auto& subscribers = subscriptions_[channel];
  auto [_, inserted] = subscribers.insert(subscriber);
  // Newly owned by THIS registry (needs an upstream SSUBSCRIBE) iff this subscriber was the 0 -> 1
  // transition for the channel.
  return inserted && subscribers.size() == 1;
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
  // pending wake does not fire against a torn-down registry (E4).
  ack_scheduler_.clear();
  control_ledger_.clearAll();
  pending_resubscribe_channels_.clear();
  if (resubscribe_generation_timer_ != nullptr) {
    resubscribe_generation_timer_->disableTimer();
  }

  // Close affected downstream connections last, after the registry maps above are emptied. The
  // active-subscriptions gauge is owned by the subscriber now (A-2): the channels THIS registry
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
        // A1-f1: this registry never owned THIS subscriber for THIS channel — a multi-registry
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
                                const DownstreamSubscriberPtr& subscriber) {
  // addSubscription mirrors the channel into the subscriber's set and returns whether THIS registry
  // now newly owns it (0 -> 1 subscribers). One channel per call, so there is no batch step
  // machinery: the ack count is simply the subscriber's total AS OF this channel (Redis's per-step
  // semantics), which is correct by construction — removing the fresh+dedup step skew (S-2 / G13).
  // Whether THIS call newly added the channel to the subscriber's OWN set — false for a DUPLICATE
  // SUBSCRIBE from a subscriber that already holds it. Captured BEFORE addSubscription mutates the
  // set, and threaded onto the pending-ack entry (below): a failed (re-)subscribe rolls back ONLY
  // the entries that newly established a subscription, never a duplicate whose live subscription it
  // merely echoes (#1). A duplicate still PARKS (so it is never acked prematurely — F1/G5), it is
  // just not torn down on failure.
  const bool subscriber_newly_added = !subscriber->subscribedChannels().contains(channel);
  const bool first_subscriber = addSubscription(channel, subscriber);
  const uint64_t count = subscriber->totalSubscriptionCount();
  if (first_subscriber) {
    // First subscriber for this channel on this thread — issue the sharded upstream subscribe and
    // park the downstream ack until the upstream confirms.
    ENVOY_LOG(debug, "redis: ssubscribing channel '{}' to correct shard", channel);
    const Upstream::HostConstSharedPtr chosen_host =
        upstream_callbacks_.sendUpstreamSsubscribe(channel, *this);
    if (!chosen_host) {
      ENVOY_LOG(warn, "redis: upstream ssubscribe failed for '{}', rolling back", channel);
      removeSubscriptions(absl::MakeConstSpan(&channel, 1), subscriber);
      return {/*success=*/false, subscriber->totalSubscriptionCount(), /*ack_deferred=*/false};
    }
    // Park the downstream ack keyed by channel; the eventual downstream ack always emits the client
    // verb ``subscribe`` (see deliverPendingSubscribeAck) since cluster sharding is transparent.
    // ``newly_added=true``: the first subscriber ON THIS REGISTRY establishes the subscription
    // here. (Exotic edge, same class as A1-f1: a subscriber that ALREADY holds this channel on a
    // DIFFERENT registry — a mid-flight prefix-route re-home — is "first" here yet not newly added
    // to its cross-registry set, so a failed reissue would roll back a reference it still holds
    // elsewhere. Faithful handling needs registry-scoped vs subscriber-scoped rollback separation;
    // left as-is because it matches prior behavior and no such re-home path exists today.)
    registerPendingSubscribeAck(channel, subscriber, count, /*newly_added=*/true);
    // A non-null return is the chosen shard owner (no separate success flag), so record it
    // unconditionally as the channel's current attempt.
    recordSsubscribeAttempt(channel, chosen_host);
    return {/*success=*/true, count, /*ack_deferred=*/true};
  }
  // Dedup, but the channel may not be confirmed on its upstream yet. A SINGLE find on
  // pending_subscribe_acks_ decides which case and, for the common one, joins the bucket without a
  // second hash (the former ``contains(channel)`` + registerPendingSubscribeAck's ``[channel]``
  // hashed the same key twice). ``subscriber_newly_added`` tags whether THIS call established the
  // subscription: a DUPLICATE (false) still parks so it is never acked early, but a failed
  // (re-)subscribe will not roll back the live subscription it merely echoes (#1).
  if (auto it = pending_subscribe_acks_.find(channel); it != pending_subscribe_acks_.end()) {
    // Still SUBSCRIBING — a prior subscriber's pending bucket is open. Join it (reuses its
    // ack-timeout schedule entry) so this subscriber is acked when the upstream ack lands, not
    // prematurely (F1/G5).
    appendPendingSubscribeAck(it->second, channel, subscriber, count, subscriber_newly_added);
    return {/*success=*/true, count, /*ack_deferred=*/true};
  }
  if (pending_resubscribe_channels_.contains(channel)) {
    // RE-subscribing after a connection loss / slot move (upstream ack not yet re-confirmed) —
    // there is no pending bucket yet, so open one and arm its ack timeout (F1/G5).
    registerPendingSubscribeAck(channel, subscriber, count, subscriber_newly_added);
    return {/*success=*/true, count, /*ack_deferred=*/true};
  }
  // Dedup on an already-ACTIVE channel (its upstream ack has landed and drained the bucket) — no
  // upstream ack will fire for this subscriber, so the splitter fabricates the ack immediately.
  return {/*success=*/true, count, /*ack_deferred=*/false};
}

uint64_t SubscriptionRegistry::unsubscribe(absl::Span<const std::string> channels,
                                           const DownstreamSubscriberPtr& subscriber,
                                           std::vector<Common::Redis::RespValue>* preserved_acks) {
  for (const auto& channel : channels) {
    // Redis-compatible SUBSCRIBE-then-UNSUBSCRIBE (Issue 4): if this channel's SUBSCRIBE is still
    // awaiting its upstream ack, preserve that ``subscribe`` ack rather than silently dropping it —
    // Redis replies ``subscribe ch <n>`` then ``unsubscribe ch <n-1>``. We COLLECT it here (NO
    // downstream write) into the caller's buffer so the splitter flushes it — ahead of its own
    // unsubscribe ack — only after its terminal respond(); delivering mid-teardown would let a
    // synchronous downstream close re-enter and mutate the very pending bucket we are draining
    // (reentrancy/UAF). On an already-ACTIVE channel (bucket drained) this is a no-op. The F1
    // rollback (failPendingSubscribers) passes a null buffer — its bucket was already erased — and
    // simply drops the entry, so the subscriber still gets the rollback (connection close, F3), not
    // a spurious success.
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
  // EXPLICIT retry scope pending_resubscribe_channels_ (D1) — not an implicit "owner-less subset".
  // Callers that have ALREADY seeded the scope with just the channel(s) they need re-resolved — an
  // unsolicited SUNSUBSCRIBE that forgot+marked its one migrated channel, or a re-issued-SSUBSCRIBE
  // error that forgot+marked its channel — call this directly so the rest of the host's healthy
  // channels keep their mapping; whole-host callers go through scheduleResubscribeForHost.
  if (subscriptions_.empty()) {
    resubscribe_backoff_.reset();
    return;
  }
  // B-1 coalesce: if a re-subscribe cycle is ALREADY scheduled (the pool timer is armed but has not
  // yet fired doResubscribe), this signal rides that cycle. Every signal path enrolls its
  // channel(s) in pending_resubscribe_channels_ BEFORE calling here, and doResubscribe re-reads
  // that whole set at fire time, so a coalesced signal is not lost. Returning early avoids two
  // defects a burst of signals would otherwise cause: (a) advancing the backoff once PER SIGNAL
  // rather than once per fired cycle — a K-channel slot move (K unsolicited SUNSUBSCRIBEs in one
  // read, or K -MOVEDs for K re-issued SSUBSCRIBEs) would jump the FIRST retry from ~100ms to ~15s;
  // (b) enableTimer replacing the pending deadline, so a steady drip of signals (e.g. A1-f2's
  // generation-timeout re-arm while a pool cycle is already pending) defers the scheduled
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
  // them (D1). This replaces the former "clear the owner at signal time and let doResubscribe
  // re-send the owner-less subset" model, which opened a window (backoff 100ms..30s) where a
  // channel sat in subscriptions_ with no owner and several paths misread that gap:
  //   * an UNSUBSCRIBE / downstream disconnect could not find the owner, so it skipped the upstream
  //     SUNSUBSCRIBE and the connection's retire — leaking the subscription on a LIVE connection
  //     (S6-1); keeping the owner lets forgetChannelHost return it so the SUNSUBSCRIBE is sent;
  //   * a dedup SUBSCRIBE fell through to an unguarded immediate success ack because the channel
  //   was
  //     not yet in this set (S6-3); seeding it HERE makes subscribe()'s dedup gate defer + protect
  //     the joiner with the ack timeout;
  //   * an unsolicited-SUNSUBSCRIBE retire gate (hostHasSubscriptions) mistook the owner gap for
  //   "no
  //     subscription" and retired a connection still carrying channels (S6-4); the owner stays, so
  //     the host still reports its channels;
  //   * a still-posted owner clear could wipe a replacement connection's fresh owner (S6-5) — there
  //     is no owner clear here to race.
  // A specific host scopes the mark to THAT host's channels (F3 — one host's loss does not
  // re-SSUBSCRIBE every channel on every other host); a null host (whole-registry fallback) marks
  // every subscribed channel. The owner is refreshed (new host + new generation) when
  // reissueSsubscribe re-sends the channel, so a late ack for the pre-signal attempt is still
  // correlated by ssubscribeAckIsCurrent until then. The control FIFO is untouched here (A-4/S-1):
  // connection-loss callers clear it separately (forgetHostConnectionLedger); connection-kept
  // control-error callers keep it.
  if (host == nullptr) {
    for (const auto& [channel, _] : subscriptions_) {
      pending_resubscribe_channels_.insert(channel);
    }
    return;
  }
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
  // an advisory ack for a now-dead expected SUNSUBSCRIBE (the FIFO is the single ledger — A-4/S-1).
  if (host != nullptr) {
    control_ledger_.clear(host);
  }
}

void SubscriptionRegistry::onUpstreamConnectionClose(const Upstream::HostConstSharedPtr& host) {
  forgetHostConnectionLedger(host);
  ENVOY_LOG(debug, "redis: upstream subscription connection lost, scheduling re-subscribe");
  scheduleResubscribeForHost(host);
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
  // Error, so a plain reply is middlebox / desync noise). Pop the FIFO head for BOTH to keep the
  // ledger in lockstep with the reply stream (Finding 2): leaving a non-Error un-popped desyncs the
  // FIFO by one — the next ssubscribe ack then head-mismatches and its pending subscribe hangs to
  // the ack timeout. For a SUNSUBSCRIBE the pop also IS the drop of its expected-ack bookkeeping
  // (A-4/S-1: the FIFO is the single ledger), so a later genuine unsolicited SUNSUBSCRIBE is not
  // swallowed as a lingering advisory expectation.
  const std::optional<PendingControlCommand> reply = control_ledger_.takeReply(host);

  // A non-Error reply carries no actionable failure — unlike the Error path below, neither
  // correlate nor fail the popped command; keep the connection open and just re-resolve this host
  // on backoff (G6). The head is already consumed above, so the FIFO stays balanced.
  if (!is_error) {
    ENVOY_LOG(debug, "redis: non-Error subscription control reply, scheduling host-scoped "
                     "re-subscribe (connection kept open)");
    scheduleResubscribeForHost(host);
    return;
  }

  // -MOVED / -ASK / -CLUSTERDOWN: the local slot map is stale, so a plain re-subscribe would just
  // draw the same redirect. Refresh the cluster topology (throttled by the shared refresh manager)
  // and re-resolve on backoff once it lands. Redis error codes are uppercase, so match the prefix.
  if (absl::StartsWith(error_str, "MOVED ") || absl::StartsWith(error_str, "ASK ") ||
      absl::StartsWith(error_str, "CLUSTERDOWN")) {
    // The redirect proves the local slot map is stale, so refresh it (throttled) regardless.
    upstream_callbacks_.requestTopologyRefresh();
    // A1-f3 + S6-6: a redirect is ACTIONABLE — re-mark this host's channels for re-subscribe on the
    // escalating backoff — ONLY when it correlates to the channel's CURRENT SSUBSCRIBE attempt on
    // this host. Ignore it otherwise (the refresh above already handles the stale slot map),
    // because:
    //   * it correlates to a SUNSUBSCRIBE (verb != "ssubscribe"): we always forgetChannelHost
    //   BEFORE
    //     sending a channel's SUNSUBSCRIBE (the channel had already moved off this host), so a
    //     -MOVED for it is STRUCTURALLY always stale. Re-marking would needlessly re-SSUBSCRIBE
    //     this host's healthy channels and open a duplicate/gap window. The generic-error branch
    //     below already ignores this exact case via its owner check
    //     (subscriptions_/ssubscribeAckIsCurrent); this removes the asymmetry (A1-f3);
    //   * it correlates to a SUPERSEDED SSUBSCRIBE (an A -> B -> A entry whose host matches but
    //     generation does not, or a channel that already re-resolved off this host) — the channel
    //     has already moved on (S6-6).
    // The FIFO head was already consumed above, so just return. A redirect with NO correlated
    // command (empty FIFO — middlebox / desync noise) still falls through to the host re-subscribe.
    if (reply.has_value() && (reply->verb != "ssubscribe" ||
                              !ssubscribeAckIsCurrent(reply->channel, host, reply->generation))) {
      ENVOY_LOG(debug,
                "redis: ignoring stale '{}' redirect for '{}' (not a current SSUBSCRIBE attempt)",
                error_str, reply->channel);
      return;
    }
    ENVOY_LOG(debug, "redis: subscription control redirect '{}', scheduling host re-subscribe",
              error_str);
    scheduleResubscribeForHost(host);
    return;
  }

  // A normal error to an SSUBSCRIBE we sent (ACL/CROSSSLOT/unknown-command): fail that channel's
  // pending downstream subscribers IMMEDIATELY (gauge rollback + connection close, F3) instead of
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
    // timeout (#1). If the channel still has active subscribers here — the mixed case, or a plain
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
    // nothing is subscribed upstream to leak (unlike the S6-1 window, where the channel WAS
    // subscribed on a live connection).
    ENVOY_LOG(debug, "redis: SSUBSCRIBE for active channel '{}' failed ('{}'), re-resolving",
              reply->channel, error_str);
    forgetChannelHost(reply->channel);
    // A1-1: if that was this host's LAST channel, its dedicated subscription connection is now
    // idle. Every OTHER last-channel path retires it; do so here too instead of leaking an idle
    // connection
    // + its Redis-side state until an unrelated event closes it. Clear the host's control ledger
    // iff the pool ACTUALLY retired (its returned decision — ALT-1, no local re-derivation of the
    // retire predicate) so a stale FIFO head does not black out a future subscribe routed back here
    // (G1/F2).
    if (upstream_callbacks_.retireSubscriptionConnectionIfIdle(host)) {
      control_ledger_.clear(host);
    }
    scheduleResubscribe();
    return;
  }

  // A SUNSUBSCRIBE error, or an error with no correlated command: keep the connection and
  // re-resolve this host's channels on backoff. A transient error recovers; a permanent one keeps
  // escalating. (Non-Error replies returned early above, so this is always a genuine error.)
  //
  // S6-6: if the error DID correlate to a specific channel this host NO LONGER owns (it already
  // re-resolved elsewhere — a stale SUNSUBSCRIBE error, or any leftover superseded attempt), the
  // operation is moot; ignore it rather than re-mark this host's healthy channels for a needless
  // re-SSUBSCRIBE. An uncorrelated error (no reply) has no channel to check and falls through to
  // the conservative host re-mark.
  if (reply.has_value()) {
    auto owner_it = channel_hosts_.find(reply->channel);
    if (owner_it == channel_hosts_.end() || owner_it->second.host != host) {
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
    resubscribe_backoff_.reset();
    if (resubscribe_generation_timer_ != nullptr) {
      resubscribe_generation_timer_->disableTimer();
    }
    // No resubscribe scope needs clearing: the retry scope is the EXPLICIT
    // pending_resubscribe_channels_ set (D1), not the owner-less subset of channel_hosts_, and once
    // subscriptions_ is empty any stale entry is inert — doResubscribe re-issues nothing for a
    // channel with no live subscription. A later single-host loss thus cannot widen into a
    // whole-registry storm (no all-flag / pending-host field survives — the former G8 hazard).
    return;
  }

  // Re-issue exactly the channels in the EXPLICIT re-subscribe signal set (D1): every signal path
  // (connection loss, control error, topology fallback, unsolicited invalidation) seeds
  // pending_resubscribe_channels_ AT SIGNAL TIME, so this set — not an implicit "owner-less subset
  // of channel_hosts_" — IS the "which channels to re-send" scope. That is what keeps the owner
  // alive through the backoff window (D1: closes the S6-1/3/4/5 gaps). Snapshot first:
  // reissueSsubscribe re-inserts each channel (idempotent), so iterating the live set would
  // invalidate under us. A channel that was fully unsubscribed since the signal is dropped from the
  // set here (unsubscribe / removeSubscriber already erase it, so this is belt-and-suspenders).
  // Only the upstream SENDS are scoped; a single host's loss seeded only ITS channels (F3), so this
  // does not re-SSUBSCRIBE every channel on every other host.
  std::vector<std::string> affected(pending_resubscribe_channels_.begin(),
                                    pending_resubscribe_channels_.end());

  bool success = true;
  // reissueSsubscribe re-resolves each channel's CURRENT owner and stamps a fresh generation, so a
  // stale owner recorded at signal time (a since-departed host) is replaced by the live one; it
  // also re-inserts the channel into pending_resubscribe_channels_ so a still-failing channel keeps
  // its generation-timeout retry + backoff protection (F7). Entries leave the set when a
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
    // closes the subscriber connection (F3), so a permanent rejection surfaces as a connection
    // close instead of hanging forever — operators should still run a Redis 7.0+ / compatible
    // upstream for steady-state sharded pub/sub.
    ENVOY_LOG(debug, "redis: re-subscribe SSUBSCRIBE(s) sent, awaiting upstream ack");
    armResubscribeGenerationTimer();
  } else {
    // A send failed (no healthy host / conn-pool hiccup). The failed channels stay in
    // pending_resubscribe_channels_ (reissueSsubscribe enrolled them before the send), so just
    // re-arm the escalating backoff to retry exactly them (S6-2). Do NOT fall back to the
    // whole-registry onUpstreamConnectionClose(): that re-marks and re-SSUBSCRIBEs every OTHER
    // host's healthy channels too, and — now that owners are kept — would leave the
    // successfully-rerouted channels' in-flight acks intact but pointlessly re-send them, a
    // re-subscribe storm on a single channel's failure.
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
  // Re-resolve ONLY the still-unacked channels of this generation (Finding 3), not every channel:
  // they are exactly the ones still in pending_resubscribe_channels_ (a fully-acked generation
  // empties the set and disables this timer). doResubscribe re-issues that set — reissueSsubscribe
  // re-resolves each channel's CURRENT owner and stamps a fresh generation, so an owner that went
  // stale is replaced WITHOUT clearing it here (D1: the owner must stay live through the backoff
  // window so a concurrent unsubscribe still sends its SUNSUBSCRIBE, a dedup subscribe still
  // defers, and the host's connection is not prematurely retired). The scheduled doResubscribe
  // re-sends exactly these on the still-escalating backoff and re-arms this timer — a bounded retry
  // loop to the 30s cap.
  scheduleResubscribe();
}

bool SubscriptionRegistry::reissueSsubscribe(
    const std::string& channel, const Upstream::HostConstSharedPtr& pre_resolved_host) {
  // Enroll in the current generation unconditionally: a channel whose send FAILS never acks, so it
  // keeps this generation incomplete and blocks a sibling's ack from resetting the backoff (F2) /
  // disarming the generation timer (Issue 3) while it is still failing.
  pending_resubscribe_channels_.insert(channel);

  // E-8: when the caller already resolved this channel's owner in the SAME synchronous iteration
  // (onClusterTopologyChange's dry-resolve), send straight to that host instead of paying a second
  // CRC16 + LB-context resolve inside the conn pool. The two would resolve identically — no
  // dispatcher yield separates them — so this is a pure work saving, not a semantic change.
  if (pre_resolved_host != nullptr) {
    if (!upstream_callbacks_.sendUpstreamSsubscribeToHost(channel, *this, pre_resolved_host)) {
      return false;
    }
    recordSsubscribeAttempt(channel, pre_resolved_host);
    return true;
  }

  const Upstream::HostConstSharedPtr host =
      upstream_callbacks_.sendUpstreamSsubscribe(channel, *this);
  if (!host) {
    return false;
  }
  // E-1: if the channel had a KEPT owner (D1) on a DIFFERENT host than the one we just re-resolved
  // to, that old host still carries the upstream shard subscription. recordSsubscribeAttempt below
  // would silently drop its last owner reference (its internal forgetChannelHost) WITHOUT sending
  // the SUNSUBSCRIBE or retiring it — leaking the old subscription: duplicate delivery on a
  // standalone / ring-hash upstream that has no Redis migration-invalidation, plus a forever-idle
  // connection. Pair it here exactly as the topology reroute does (forget + courtesy SUNSUBSCRIBE,
  // which is find-only so a dead old connection is a harmless no-op) before recording the new
  // owner.
  const auto owner_it = channel_hosts_.find(channel);
  if (owner_it != channel_hosts_.end() && owner_it->second.host != host) {
    const Upstream::HostConstSharedPtr old_host = owner_it->second.host;
    forgetChannelHost(channel);
    sendSunsubscribe(channel, old_host);
  }
  // Record the host we sent to AND a fresh generation as the channel's current attempt (the
  // non-null return is that host): onPushMessage correlates the SSUBSCRIBE ack against both (Issue
  // 2/3) so an ack from a superseded host OR a superseded same-host attempt (A -> B -> A) neither
  // completes a pending downstream subscribe nor advances this generation.
  recordSsubscribeAttempt(channel, host);
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
    resubscribe_generation_timer_->enableTimer(subscribe_ack_timeout_);
  }
}

void SubscriptionRegistry::dropHost(const Upstream::HostConstSharedPtr& host) {
  // The host is gone; none of its outstanding control commands (SSUBSCRIBE/SUNSUBSCRIBE) will ack
  // or error, so drop the whole per-host control FIFO — the single ledger (A-4/S-1).
  control_ledger_.clear(host);
  // Seed this host's channels into the explicit re-subscribe retry scope (D1) BEFORE dropping their
  // owner, so a new SUBSCRIBE for one of them in the (short) window before the posted
  // onClusterTopologyChange re-routes it is deferred + ack-timeout-protected by subscribe()'s dedup
  // gate rather than handed a premature success (S6-3).
  markHostChannelsForResubscribe(host);
  // Then owner-less every channel this host served. The host is REMOVED, so keeping the owner is
  // pointless (a SUNSUBSCRIBE to it is a find-only no-op now — F2) and would mislead
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
  // ``SSUBSCRIBE`` to the new host, without waiting for the unsubscribe ack. This favors
  // continuity (no message gap during a slot move) over strictly avoiding duplicate
  // delivery: there is a brief window where both hosts may push the same message, and a
  // subscriber may see one duplicate. The alternative — chaining ``SSUBSCRIBE`` after the
  // unsubscribe ack arrives — eliminates the duplicate window but introduces a real
  // delivery gap on every topology change. Pub/sub semantics already permit best-effort
  // delivery, and Redis itself does not promise no duplicates across slot migrations, so
  // the gap-free choice is the expected one. Reviewers asking about strict no-duplicate
  // delivery should see this comment.
  for (const auto& [channel, _] : subscriptions_) {
    auto old_it = channel_hosts_.find(channel);
    const Upstream::HostConstSharedPtr old_host =
        (old_it != channel_hosts_.end()) ? old_it->second.host : nullptr;

    // Dry-resolve the channel's CURRENT slot owner without sending anything. If it is unchanged,
    // leave the subscription untouched: re-issuing SUNSUBSCRIBE+SSUBSCRIBE on an unmoved channel
    // wastes upstream control traffic AND opens a needless message gap / duplicate window on a
    // channel nothing moved. A slot-only rebalance moves only some slots, so most active channels
    // are unaffected and must be a no-op here.
    const Upstream::HostConstSharedPtr new_host =
        upstream_callbacks_.chooseUpstreamHostForChannel(channel);
    // A TRANSIENT nullptr resolve (resharding window, momentary master absence) is not proof the
    // owner moved — a genuine ownership loss arrives separately as Redis's unsolicited
    // SUNSUBSCRIBE. So when the channel still has a healthy old owner, leave it untouched on an
    // UNKNOWN resolve as well as an unchanged one; only a resolve to a DIFFERENT known host tears
    // the old owner down (G4). Without this, a momentary resolve failure would SUNSUBSCRIBE a live
    // owner, fail the reissue, and cascade (any_failed -> onUpstreamConnectionClose) into a
    // whole-registry re-resolution. An owner-less channel (old_host == nullptr) still falls through
    // to (re)resolve.
    if (old_host != nullptr && (new_host == nullptr || new_host == old_host)) {
      continue;
    }

    // Owner moved (or old/new unknown): drop the stale mapping, SUNSUBSCRIBE the old owner, then
    // re-SSUBSCRIBE the new one. forgetChannelHost erases the old mapping BEFORE the SUNSUBSCRIBE
    // so any subscription-mode cleanup along the send path observes the up-to-date map and tears
    // down push_callbacks_ on the old upstream client. (C4)
    if (auto forgotten = forgetChannelHost(channel)) {
      sendSunsubscribe(channel, forgotten);
    }
    any_reissued = true;

    // Reissue via the shared helper so the rerouted channel is enrolled in the resubscribe
    // generation (and records its new expected ack owner) exactly like the connection-loss path —
    // previously this reroute had no ack timeout, so a silently-lost SSUBSCRIBE ack stranded the
    // channel with no retry (Issue 4). Hand off the host we just dry-resolved (E-8) so the send
    // does not re-resolve; a null new_host (owner-less re-resolve) falls back to the resolving
    // send.
    if (!reissueSsubscribe(channel, new_host)) {
      ENVOY_LOG(warn, "redis: topology change: failed to re-route ssubscribe for '{}'", channel);
      any_failed = true;
    }
  }
  // Arm the generation ack timeout ONLY for a pass that actually rerouted a channel (A1-f2). The
  // rerouted channels clear from pending_resubscribe_channels_ as their new owners ack
  // (host-correlated in onPushMessage), and if any is still unacked when it fires,
  // handleResubscribeGenerationTimeout re-resolves it on backoff. When this pass reissued NOTHING
  // (every active channel's owner was unchanged — the common slot-only rebalance), the set may
  // still hold channels enrolled by an UNRELATED prior signal that a pool backoff cycle already
  // owns; arming here would (re)start a generation clock over those channels and, on timeout, call
  // scheduleResubscribe — needless churn that the B-1 coalesce now absorbs but that should not be
  // triggered in the first place. So only arm when this pass owns channels in the generation.
  if (any_reissued) {
    armResubscribeGenerationTimer();
  }
  // On a planned host removal the pool suppresses onUpstreamConnectionClose to avoid a double
  // resubscribe (C-6), so this sharded re-route is the sole re-router for channels whose slot owner
  // just changed. If any re-route SEND failed, retry exactly those on backoff: reissueSsubscribe
  // enrolled each failed channel in pending_resubscribe_channels_ before its send, so a plain
  // scheduleResubscribe re-sends them (S6-2). Do NOT fall back to the whole-registry
  // onUpstreamConnectionClose(): it would re-mark every OTHER channel too — including the ones that
  // just rerouted SUCCESSFULLY — pointlessly re-SSUBSCRIBE-ing them and staling their in-flight
  // acks.
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
  // buffer/encoder — E3) and share those bytes with each — turns N tree-walks + N length-formats
  // into 1, and (E-2) N full payload copies into 1 copy (the encode->string below) plus a small
  // per-subscriber zero-copy fragment. InlinedVector keeps the common small fan-out off the heap.
  // Snapshot into the reused member vector (E-3): a > 8-subscriber channel keeps its heap buffer
  // across messages instead of allocating one per message. Cleared at the end so it never pins
  // subscribers between deliveries. Same non-nesting invariant as fanout_encoder_/fanout_buffer_.
  fanout_targets_.assign(subscribers.begin(), subscribers.end());
  fanout_encoder_.encode(frame, fanout_buffer_);
  // One linearizing copy of the encode into a refcounted string that every subscriber's fragment
  // references; freed once the last fragment drains (E-2). Drain the scratch buffer immediately.
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
    // maps only and never moves or mutates ``value``, so array[1] stays valid — no per-ack copy
    // (EFF-6).
    const std::string& target = array[1].asString();
    // Drop this ack's entry from the host's outstanding-control FIFO FIRST, and learn WHICH attempt
    // (generation) it acked: acks return in per-connection FIFO order, so the oldest outstanding
    // (ssubscribe, target) send on this host is the one being acked. (Also keeps a later error from
    // misattributing itself to an already-acked command.)
    const std::optional<uint64_t> acked_generation =
        control_ledger_.consumeAck(host, "ssubscribe", target);
    // An ack completes/advances the channel only if it is for the CURRENT attempt — same owning
    // host AND same generation. This one predicate rejects an owner-less-gap ack (Issue 2), a
    // wrong-host ack, and a SAME-HOST stale ack from a superseded attempt (A -> B -> A, Issue 3)
    // whose host alone would pass. A stale ack must neither advance the resubscribe generation /
    // disarm its timer nor complete the pending downstream subscribe.
    const bool ack_is_current = ssubscribeAckIsCurrent(target, host, acked_generation);
    // Reset the re-subscribe backoff only when the CURRENT resubscribe generation has fully acked
    // (F2). Resetting on any single channel's ack would let a partial failure — some channels ack
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
    //     left, or a topology re-route). Those live as the (sunsubscribe, channel) entry the send
    //     recorded on the host's outstanding-control FIFO, so consume one here and leave state
    //     alone.
    //  2. UNSOLICITED — this node lost ownership of the channel's slot (Redis resharding:
    //     pubsubShardUnsubscribeAllChannelsInSlot -> addReplyPubsubUnsubscribed) and dropped our
    //     shard subscription while the channel is STILL active here. Forget the now-stale
    //     channel->host mapping so it is re-resolved; leave ``subscriptions_`` intact.
    // The expected-ack check MUST come first: a topology change / rapid re-subscribe can leave a
    // channel active AND our own SUNSUBSCRIBE ack still in flight — without it we would mistake our
    // ack for an invalidation and wrongly forget the freshly-set host. We do NOT re-subscribe here:
    // the local slot map may still point at the old owner, so re-SSUBSCRIBE-ing would draw another
    // unsolicited sunsubscribe (a loop); the slot-map refresh -> onClusterTopologyChange re-routes
    // the whole subscription set to current owners.
    if (array.size() >= 2 && isRespString(array[1])) {
      const std::string& channel = array[1].asString();
      // ADVISORY ack vs UNSOLICITED invalidation is decided by the host's outstanding-control FIFO
      // alone (A-4/S-1): if its head is the ``sunsubscribe`` we sent for this channel, this is our
      // own ack — consume it and leave the mapping. Every ssubscribe/sunsubscribe ack AND every
      // control error pops the FIFO head (acks return in per-connection FIFO order), so the head is
      // exactly the oldest command still awaiting a reply — a head match therefore means an
      // outstanding expected SUNSUBSCRIBE ack, which the former separate
      // ``pending_sunsubscribe_acks_`` count merely mirrored. ``host`` is always the known source
      // host (S-4), so the FIFO lookup is exact.
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
        // topology trigger. ``host`` is the known source host (S-4); compare it to the current
        // owner.
        auto host_it = channel_hosts_.find(channel);
        if (host_it != channel_hosts_.end() && host_it->second.host != host) {
          ENVOY_LOG(debug, "redis: stale 'sunsubscribe' for '{}' from a non-owning host, ignoring",
                    channel);
          // A1-2: the channel already re-resolved off this SOURCE host, so this late/duplicate
          // unsolicited SUNSUBSCRIBE proves the source no longer owns it. If it was the source's
          // last channel, its dedicated subscription connection is now idle — retire it (every
          // other last-channel path does), and clear its control ledger iff the pool ACTUALLY
          // retired (its returned decision — ALT-1) so a stale FIFO head does not black out a
          // future subscribe.
          if (upstream_callbacks_.retireSubscriptionConnectionIfIdle(host)) {
            control_ledger_.clear(host);
          }
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
        // than let it linger until an unrelated event closes it (Issue 5). Do this BEFORE
        // scheduling the re-resolve: the channel re-subscribes to its NEW owner, not this departed
        // one.
        //
        // G1: retiring the connection closes it, so any control command still outstanding for this
        // host — e.g. a client UNSUBSCRIBE of ANOTHER channel whose ack is still in flight — will
        // never arrive. Clear the host's whole control ledger iff the pool ACTUALLY retired (its
        // returned decision — ALT-1, so the clear tracks the pool's real retire rather than
        // re-deriving the predicate here) so the stale FIFO head cannot black out a future
        // subscribe routed back here (same failure mode as F2). A host that still owns channels
        // keeps its connection and its legitimately-outstanding acks.
        if (upstream_callbacks_.retireSubscriptionConnectionIfIdle(host)) {
          control_ledger_.clear(host);
        }
        // The slot moved off this owner, so the local slot map is stale — a plain re-SSUBSCRIBE
        // would draw the same invalidation. Refresh the cluster topology (throttled) AND schedule a
        // backoff re-resolve, so the channel re-subscribes to its NEW owner promptly instead of
        // sitting in subscriptions_ with no upstream subscription until an unrelated topology event
        // fires. forgetChannelHost cleared the mapping because the slot GENUINELY moved off this
        // owner, so ADD the channel to the explicit re-subscribe signal set — doResubscribe
        // re-resolves exactly that set (D1). This is the one owner-less-at-signal case that is
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
    // classic ``subscribe`` / ``unsubscribe`` here — the former dedicated branch for those was dead
    // (S-8).
    ENVOY_LOG(debug, "redis: unexpected upstream push message type '{}', dropping", msg_type);
  }
}

void SubscriptionRegistry::registerPendingSubscribeAck(const std::string& target,
                                                       const DownstreamSubscriberPtr& subscriber,
                                                       uint64_t snapshot_count, bool newly_added) {
  appendPendingSubscribeAck(pending_subscribe_acks_[target], target, subscriber, snapshot_count,
                            newly_added);
}

void SubscriptionRegistry::appendPendingSubscribeAck(PendingBucket& bucket,
                                                     const std::string& target,
                                                     const DownstreamSubscriberPtr& subscriber,
                                                     uint64_t snapshot_count, bool newly_added) {
  const bool new_bucket = bucket.entries_.empty();
  bucket.entries_.push_back(
      {std::weak_ptr<DownstreamSubscriber>(subscriber), snapshot_count, newly_added});
  // Schedule the subscribe-ack timeout (F4). If the upstream never acks this subscribe, the timeout
  // fails it downstream and rolls it back instead of hanging the SUBSCRIBE forever. A subscriber
  // that JOINS a still-pending bucket waits on the same upstream ack, so it reuses the bucket's
  // existing schedule entry — only a FRESH bucket needs a new one. All buckets share ONE timer
  // (E4): the deadline is constant, so a new entry's deadline is always >= the last, keeping
  // subscribe_ack_schedule_ ordered.
  if (!new_bucket) {
    return;
  }
  // A FRESH bucket gets a deadline; store the scheduler's token so its live-predicate can tell this
  // registration from a later re-subscribe under a new token (D3). The scheduler arms the shared
  // timer on the empty -> non-empty edge.
  bucket.schedule_seq_ = ack_scheduler_.schedule(target);
}

bool SubscriptionRegistry::failPendingSubscribers(const std::string& target,
                                                  const std::string& error_message) {
  auto it = pending_subscribe_acks_.find(target);
  if (it == pending_subscribe_acks_.end()) {
    // No pending downstream bucket for this channel: nothing to fail. Returns false so the
    // caller can tell a failed FRESH subscribe (had pending subscribers) apart from an error to a
    // re-issued SSUBSCRIBE for an already-ACTIVE channel (no pending bucket — needs re-resolution,
    // not a downstream failure). Also a defensive no-op when the ack-timeout drained a schedule
    // entry whose bucket was already acked (ack_scheduler_ only fires this on a token match, but
    // the guard keeps every caller safe).
    return false;
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
  // roll back ONCE per (subscriber, channel), not once per bucket entry (MISC-2). A duplicate
  // pipelined SUBSCRIBE (``SUBSCRIBE ch ch``) parks TWO entries for the SAME subscriber on this one
  // channel, yet the channel was added to the pubsub_active_subscriptions gauge only once
  // (addChannel dedups) and is rolled back only once — so recording pubsub_subscribe_ack_error per
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
      // handles), so it is NOT this failed attempt's to roll back or close (#1). Leave it
      // subscribed; the channel's re-resolution in the caller restores its upstream. But a
      // duplicate SUBSCRIBE still deserves an ack — real Redis acks it unconditionally, and the
      // SUCCESS path delivers one (SW-4). So if the subscription SURVIVES this failure (the
      // subscriber still holds the channel — i.e. a same-subscriber fresh entry did not just roll
      // it back and close), emit the echo-ack now rather than dropping the duplicate's SUBSCRIBE
      // unanswered. (A fresh
      // ``SUBSCRIBE ch ch`` whose first entry closed the subscriber leaves ``contains`` false here,
      // so the duplicate is correctly moot.)
      if (subscriber->subscribedChannels().contains(target)) {
        subscriber->deliver(makeSubscriptionAck("subscribe", &target, entry.snapshot_count));
      }
      continue;
    }
    if (!subscriber->subscribedChannels().contains(target)) {
      continue; // already failed for this subscriber+channel by an earlier (duplicate) entry
    }
    subscriber->recordSubscribeAckError();
    // Roll back this subscriber's failed channel BEFORE closing. unsubscribe() removes the channel
    // through removeChannel, which decrements the pubsub_active_subscriptions gauge (A-2) — so the
    // optimistic increment made when the channel was added is undone here, with no separate
    // rollback accounting. The gauge drops exactly once (the guard above already skipped
    // duplicates).
    unsubscribe(absl::MakeConstSpan(&target, 1), subscriber);
    // F3: close the subscriber's connection instead of writing a bare out-of-band ``-ERR``. An
    // unsolicited Error frame on a RESP3 connection has no reply to attach to, so a pipelining
    // client attributes it to the wrong (earlier, still-pending) command — permanently desyncing
    // its request/response matching. Closing is protocol-clean and consistent with slow-subscriber
    // eviction; the resulting disconnect cleans up the subscriber's remaining subscriptions. (The
    // ``bucket`` was moved out and erased above, so this close-driven removeSubscriber cannot
    // mutate the entries we are iterating.)
    subscriber->close();
  }
  return true;
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
  // ack — owner-less gap (Issue 2), wrong host, or a superseded same-host attempt A -> B -> A
  // (Issue 3) — must leave the bucket parked for the current attempt's ack rather than hand the
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
  // lingering until its deadline and waking the timer to a token-miss no-op (G14).
  ack_scheduler_.pruneAndRearm();
  // Build the ack skeleton ``["subscribe", target, count]`` once: verb and target are identical for
  // every entry in this bucket (same channel), so only the trailing per-subscriber Integer count
  // varies between subscribers. The downstream verb is always the literal ``subscribe`` (not the
  // upstream verb ``ssubscribe``): the client only ever issued ``SUBSCRIBE`` and the splitter
  // rewrote it to ``SSUBSCRIBE`` internally, so cluster sharding stays invisible.
  Common::Redis::RespValue ack =
      makeSubscriptionAck("subscribe", &target, /*subscription_count=*/0);
  for (auto& entry : bucket.entries_) {
    auto sub = entry.subscriber.lock();
    if (!sub) {
      continue; // disconnected before ack arrived
    }
    // Use the snapshot count from subscribe-call time (matches Redis's "number of subscriptions the
    // client now has at THIS step" semantics). Upstream's echoed count is ignored — it's the
    // registry-wide distinct channel count, wrong for a per-subscriber ack. Only this trailing
    // Integer changes between subscribers, so mutate it in place on the shared skeleton.
    ack.asArray()[2].asInteger() = entry.snapshot_count;
    sub->deliver(ack);
    // SW-4: DELIVER an ack per bucket entry — a pipelined ``SUBSCRIBE ch ch`` gets two subscribe
    // acks, matching real Redis — but COUNT success ONCE per (subscriber, channel), symmetric with
    // the error path's MISC-2 gate. Otherwise a duplicate would inflate
    // pubsub_subscribe_ack_success by +1 while pubsub_subscribe_ack_error counts per (subscriber,
    // channel), so the two counters would be in different units and any ``attempts ~= success +
    // error`` / success-rate dashboard would drift under duplicates. The newly_added entry is the
    // one that actually established the subscription (and bumped the active gauge once); a
    // duplicate (newly_added == false) re-delivered the ack but established nothing, so it must not
    // re-count.
    if (entry.newly_added) {
      sub->recordSubscribeAckSuccess();
    }
  }
}

void SubscriptionRegistry::dropPendingForSubscriber(const DownstreamSubscriberPtr& subscriber) {
  // Scrub this subscriber (and any expired-weak entries) from every pending-ack bucket on THIS
  // registry (S-5). There is no per-subscriber ``pending_ack_keys_`` mirror to consult: the pending
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
  // Collect one ``subscribe`` ack per entry this subscriber parked on the channel (a pipelined
  // duplicate SUBSCRIBE has its own entry, each acked once — symmetric with
  // deliverPendingSubscribeAck). The downstream verb is always the literal ``subscribe``: the
  // client issued SUBSCRIBE and the splitter rewrote it to SSUBSCRIBE, so sharding stays invisible.
  // The trailing count is the subscriber's snapshot at subscribe-call time. We do NOT write here
  // and do NOT touch the subscribe-ack outcome counters (Issue 6): this is a client-cancelled
  // subscribe, not an upstream ack resolution — the caller delivers ``out_acks`` after its terminal
  // respond().
  bool collected = false;
  for (auto& entry : it->second.entries_) {
    auto sp = entry.subscriber.lock();
    if (sp != subscriber) {
      continue;
    }
    out_acks.push_back(
        makeSubscriptionAck("subscribe", &target, /*subscription_count=*/entry.snapshot_count));
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
