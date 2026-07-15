#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/event/real_time_system.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "source/extensions/filters/network/common/redis/codec.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter.h"
#include "source/extensions/filters/network/redis_proxy/external_auth.h"
#include "source/extensions/filters/network/redis_proxy/subscription_registry.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * All redis proxy stats. @see stats_macros.h
 */
#define ALL_REDIS_PROXY_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(downstream_cx_drain_close)                                                               \
  COUNTER(downstream_cx_protocol_error)                                                            \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  COUNTER(downstream_rq_noproto)                                                                   \
  COUNTER(downstream_rq_total)                                                                     \
  COUNTER(pubsub_push_messages_delivered)                                                          \
  COUNTER(pubsub_slow_subscriber_closed)                                                           \
  COUNTER(pubsub_subscribe_ack_error)                                                              \
  COUNTER(pubsub_subscribe_ack_success)                                                            \
  COUNTER(pubsub_subscribe_total)                                                                  \
  COUNTER(pubsub_unsubscribe_total)                                                                \
  GAUGE(downstream_cx_active, Accumulate)                                                          \
  GAUGE(downstream_cx_rx_bytes_buffered, Accumulate)                                               \
  GAUGE(downstream_cx_tx_bytes_buffered, Accumulate)                                               \
  GAUGE(downstream_rq_active, Accumulate)                                                          \
  GAUGE(pubsub_active_subscriptions, Accumulate)

/**
 * Struct definition for all redis proxy stats. @see stats_macros.h
 */
struct ProxyStats {
  ALL_REDIS_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for the redis proxy filter.
 */
class ProxyFilterConfig : public Logger::Loggable<Logger::Id::redis> {
public:
  ProxyFilterConfig(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& config,
      Stats::Scope& scope, const Network::DrainDecision& drain_decision, Runtime::Loader& runtime,
      Api::Api& api, TimeSource& time_source,
      Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory);

  const Network::DrainDecision& drain_decision_;
  Runtime::Loader& runtime_;
  const std::string stat_prefix_;
  const std::string redis_drain_close_runtime_key_{"redis.drain_close_enabled"};
  ProxyStats stats_;
  const std::string downstream_auth_username_;
  std::vector<std::string> downstream_auth_passwords_;
  TimeSource& timeSource() const { return time_source_; };
  // Listener-level RESP version (downstream + upstream). Fixed at config load.
  Common::Redis::RespProtocolVersion protocolVersion() const { return protocol_version_; }
  // Whether ``enable_sharded_publish`` is set: gate for the PUBLISH -> SPUBLISH rewrite.
  bool enableShardedPublish() const { return enable_sharded_publish_; }
  const bool external_auth_enabled_;
  const bool external_auth_expiration_enabled_;

  // DNS cache used for ASK/MOVED responses.
  const Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr dns_cache_manager_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_{nullptr};

private:
  static ProxyStats generateStats(const std::string& prefix, Stats::Scope& scope);
  Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr
  getCache(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& config);
  TimeSource& time_source_;
  const Common::Redis::RespProtocolVersion protocol_version_;
  const bool enable_sharded_publish_;
};

using ProxyFilterConfigSharedPtr = std::shared_ptr<ProxyFilterConfig>;

// UAF-safe back-ref from a shared DownstreamSubscriber to its per-connection ProxyFilter, so a
// MESSAGE push can be ordered behind in-flight replies. Defined in the .cc; the filter owns the
// sole shared_ptr and detaches it on destruction. See PushOrderingSink.
class ProxyFilterPushSink;

/**
 * A redis multiplexing proxy filter. This filter will take incoming redis pipelined commands, and
 * multiplex them onto a consistently hashed connection pool of backend servers.
 */
class ProxyFilter : public Network::ReadFilter,
                    public Common::Redis::DecoderCallbacks,
                    public Network::ConnectionCallbacks,
                    public Logger::Loggable<Logger::Id::redis>,
                    public ExternalAuth::AuthenticateCallback,
                    public CommandSplitter::PubsubSession {
public:
  ProxyFilter(Common::Redis::DecoderFactory& factory, Common::Redis::EncoderPtr&& encoder,
              CommandSplitter::Instance& splitter, ProxyFilterConfigSharedPtr config,
              ExternalAuth::ExternalAuthClientPtr&& auth_client);
  ~ProxyFilter() override;

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {
    // Backpressure for the pub/sub "slow consumer": a subscriber receives unsolicited Push frames
    // it cannot pace (unlike a request/response client, which self-paces via its own requests). If
    // its downstream write buffer overflows the connection's high watermark, close it rather than
    // buffer without bound — matching Redis's client-output-buffer-limit pubsub eviction. Only
    // subscriber connections are subject to this (a non-subscriber's overflow is its own
    // request/reply backlog, which it drains by reading). Idempotent: the high watermark can
    // re-fire before the close completes, and a deliver path may also observe the overflow.
    // ``subscriber_`` is created lazily on the first SUBSCRIBE and lingers after the last
    // UNSUBSCRIBE (even a bare UNSUBSCRIBE creates it), so gate on there being ACTIVE pub/sub work
    // — live subscriptions or bytes still parked behind in-flight replies — otherwise a plain
    // request/reply connection that merely once subscribed would be wrongly evicted as a slow
    // subscriber on its own reply backlog.
    if (subscriber_ != nullptr &&
        (subscriber_->totalSubscriptionCount() > 0 || held_push_bytes_ > 0) &&
        !slow_subscriber_closed_) {
      closeSlowSubscriber("downstream write buffer above high watermark");
    }
  }
  void onBelowWriteBufferLowWatermark() override {}

  // Common::Redis::DecoderCallbacks
  void onRespValue(Common::Redis::RespValuePtr&& value) override;

  // AuthenticateCallback
  void onAuthenticateExternal(CommandSplitter::SplitCallbacks& request,
                              ExternalAuth::AuthenticateResponsePtr&& response) override;

  bool connectionAllowed();

  Common::Redis::Client::Transaction& transaction() { return transaction_; }

private:
  friend class RedisProxyFilterTest;
  // Routes MESSAGE pushes back into the private enqueueOrderedPush (see ProxyFilterPushSink).
  friend class ProxyFilterPushSink;

  enum class ExternalAuthCallStatus { Pending, Ready };

  // Shared close handler for both paths: the downstream connection's own event
  // (``is_downstream=true``, via onEvent) and the transaction client's UPSTREAM connection event
  // (``is_downstream=false``, via the upstream_transaction_cb_ adapter). Only the transaction close
  // and the auth-call cancel run on both directions; the response-FIFO cleanup (cancel +
  // held_push_bytes reset) and the pub/sub subscriber teardown are downstream-only (R8-1) — on an
  // upstream transaction-client close the downstream is still live, so its FIFO and subscriber must
  // survive.
  void handleConnectionEvent(Network::ConnectionEvent event, bool is_downstream);

  // The transaction client's upstream connection registers a ConnectionCallbacks (base wiring:
  // conn_pool addConnectionCallbacks(*transaction.connection_cb_)). We give it this thin adapter
  // instead of the ProxyFilter itself, so an upstream transaction-client close (a keyed MULTI/EXEC
  // closing the transaction, or an upstream dying mid-transaction) does NOT run ProxyFilter's
  // downstream subscriber teardown — which would silently strand a live subscriber across a keyed
  // MULTI/EXEC (real Redis keeps subscriptions across transactions).
  struct UpstreamTransactionCallbacks : public Network::ConnectionCallbacks {
    explicit UpstreamTransactionCallbacks(ProxyFilter& parent) : parent_(parent) {}
    void onEvent(Network::ConnectionEvent event) override {
      parent_.handleConnectionEvent(event, /*is_downstream=*/false);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}
    ProxyFilter& parent_;
  };

  struct PendingRequest : public CommandSplitter::SplitCallbacks {
    PendingRequest(ProxyFilter& parent);
    ~PendingRequest() override;

    // RedisProxy::CommandSplitter::SplitCallbacks
    bool connectionAllowed() override { return parent_.connectionAllowed(); }
    void onQuit() override { parent_.onQuit(*this); }
    void onAuth(const std::string& password) override { parent_.onAuth(*this, password); }
    void onAuth(const std::string& username, const std::string& password) override {
      parent_.onAuth(*this, username, password);
    }
    void respond(CommandSplitter::RespValueFrames&& frames) override {
      parent_.respond(*this, std::move(frames));
    }

    Common::Redis::Client::Transaction& transaction() override { return parent_.transaction(); }

    AuthAttempt attemptDownstreamAuthInline(const std::string& username,
                                            const std::string& password,
                                            uint32_t requested_version) override {
      return parent_.attemptDownstreamAuthInline(*this, username, password, requested_version);
    }
    void setDownstreamRespVersion(uint32_t version) override;
    Common::Redis::RespProtocolVersion protocolVersion() const override {
      return parent_.config_->protocolVersion();
    }
    bool shardedPublishEnabled() const override { return parent_.config_->enableShardedPublish(); }

    uint32_t currentDownstreamRespVersion() const override {
      return parent_.downstream_resp_version_;
    }

    std::optional<uint32_t> takePendingHelloAuthVersion() override {
      auto version = pending_hello_auth_version_;
      pending_hello_auth_version_.reset();
      return version;
    }

    // The pub/sub session is the CONNECTION-scoped ProxyFilter, not this per-request object:
    // returning &parent_ decouples the session's lifetime from a single request's terminal
    // respond() (which destroys the PendingRequest), so a future handler edit that calls a session
    // op after respond() cannot dereference a freed PendingRequest. The session methods themselves
    // live on ProxyFilter below.
    CommandSplitter::PubsubSession* pubsub() override { return &parent_; }

    // Downstream RESP version captured at request creation, set in the constructor from the
    // parent filter's current downstream_resp_version_.
    uint32_t resp_version_at_creation_;
    ProxyFilter& parent_;
    // This value is set when the request is on hold, waiting for an external auth response.
    Common::Redis::RespValuePtr pending_request_value_;
    // Pending response frames in the order the splitter handed them to respond(). A request may
    // carry zero frames (a request that completed with nothing to write — a bare UNSUBSCRIBE on an
    // already-gone subscriber), exactly one (the common single-reply case fed via the onResponse
    // sugar), or several (a multi-channel SUBSCRIBE / UNSUBSCRIBE whose per-channel acks and inline
    // ``-ERR`` replies share this entry; a fresh SUBSCRIBE fills its ack frames as each channel's
    // upstream ack lands, then respond()s them all here). Drained in order by
    // ProxyFilter::flushReadyResponses once complete_ becomes true and the request reaches the
    // front of pending_requests_.
    CommandSplitter::RespValueFrames pending_responses_;
    // Already-RESP3-encoded MESSAGE push frames that arrived while this was the most recent
    // in-flight request. flushReadyResponses writes them right after this request's reply, so a
    // push never overtakes a reply that preceded it on a subscribed-and-publishing client (FIFO).
    // Bytes here are counted in ProxyFilter::held_push_bytes_ for backpressure. See
    // ProxyFilter::enqueueOrderedPush.
    // Lazily allocated: a Buffer::OwnedImpl carries a slice ring, so embedding one in EVERY
    // PendingRequest paid a per-command construct/destruct on the whole-traffic hot path even
    // though only a subscribed-and-publishing client ever parks a push. Created on the first park
    // (enqueueOrderedPush); a null pointer means "no parked pushes".
    std::unique_ptr<Buffer::OwnedImpl> trailing_pushes_;
    // Count of MESSAGE push frames parked in ``trailing_pushes_``. flushReadyResponses adds it to
    // ``pubsub_push_messages_delivered`` when the frames actually reach the wire, so parked frames
    // dropped on slow-subscriber eviction / disconnect (never flushed) are not double-counted.
    uint32_t trailing_push_count_{0};
    // Set to true exactly once, when the splitter calls respond() (the one-shot terminal) with the
    // request's full frame batch. The flush loop pops from the front of pending_requests_ only when
    // the front entry is complete — preserving FIFO ordering against later-completed siblings.
    bool complete_{false};
    CommandSplitter::SplitRequestPtr request_handle_;
    // When this PendingRequest is a HELLO N AUTH ... whose inline-auth check was deferred to
    // the external auth provider, holds N (the requested protocol version). On
    // onAuthenticateExternal, ProxyFilter consults this to emit the deferred HELLO Map (and
    // flip the downstream RESP version) on success or an error reply on failure, instead of
    // the +OK that the AUTH-command path emits.
    std::optional<uint32_t> pending_hello_auth_version_;
  };

  void onQuit(PendingRequest& request);
  void onAuth(PendingRequest& request, const std::string& password);
  void onAuth(PendingRequest& request, const std::string& username, const std::string& password);
  // Terminal: move the splitter's ordered frame batch (possibly empty) into
  // request.pending_responses_, mark the request complete, clear the splitter handle, and drain
  // contiguously-complete entries from the front of pending_requests_ (encoding each entry's queued
  // frames in arrival order). ASSERTs the request has not already been completed (respond() is the
  // one-shot terminal). After this returns, request may have been destroyed (popped from the FIFO)
  // — callers must not touch it.
  void respond(PendingRequest& request, CommandSplitter::RespValueFrames&& frames);
  // Drain the front of pending_requests_ while the front entry is complete: encode each entry's
  // queued frames in order, pop the entry, repeat. Single connection.write per call. Trailing
  // close-on-quit / drain-decision / transaction-close checks run after the loop so a flush
  // that empties the FIFO honors them just like a single-frame respond does.
  void flushReadyResponses();
  bool checkPassword(const std::string& password);
  // Shared local-credential policy for ``AUTH <user> <pass>`` and ``HELLO N AUTH <user> <pass>``
  // (one copy so the two auth entry points cannot silently diverge).
  bool checkCredentials(const std::string& username, const std::string& password);
  // Inline-auth path used by HELLO N AUTH ... handling. Local-credentials case returns
  // Allowed (flipping connection_allowed_) or Denied. External-auth case stashes
  // ``requested_version`` on ``request`` and kicks off ``authenticateExternal``, returning
  // ``ImplOwnsResponse``; ``onAuthenticateExternal`` then emits the deferred HELLO Map / error.
  CommandSplitter::SplitCallbacks::AuthAttempt
  attemptDownstreamAuthInline(PendingRequest& request, const std::string& username,
                              const std::string& password, uint32_t requested_version);
  void processRespValue(Common::Redis::RespValuePtr&& value, PendingRequest& request);
  // Drain any pending_request_value_ entries left in pending_requests_ after an external-auth
  // round trip resolved (called from onAuthenticateExternal). Walks the entire list in FIFO
  // order, not just the front; bails if a resumed entry starts a new round trip, and is
  // guarded against reentrant calls because a resumed AUTH can resolve synchronously (gRPC send()
  // may fail inline) and re-enter this method from within processRespValue.
  void resumeAuthHeldRequests();
  // Lazily create the per-connection downstream subscriber on the first ``SUBSCRIBE`` (the only
  // client-exposed pub/sub subscribe verb — PSUBSCRIBE is rejected and SSUBSCRIBE is internal);
  // reused for every subsequent pub/sub command.
  DownstreamSubscriberPtr getOrCreateSubscriber();

  // CommandSplitter::PubsubSession — the pub/sub session is CONNECTION-scoped, so it lives on the
  // filter, not on a per-request PendingRequest whose lifetime ends at respond(). Reached via
  // SplitCallbacks::pubsub() (PendingRequest::pubsub() returns &parent_).
  DownstreamSubscriberPtr downstreamSubscriber() override { return getOrCreateSubscriber(); }
  void setSubscriptionRegistry(const SubscriptionRegistryPtr& registry) override {
    // Add if not already tracked, and prune expired weak refs while scanning. The tracked
    // registries are conn-pool-scoped WEAK refs; a cluster-update × resubscribe cycle tears down
    // and replaces conn pools, expiring their registries. The former dedup (existing.lock() ==
    // registry) never matches an expired entry (its lock() is null, the incoming registry is not),
    // so dead entries accumulated monotonically and every UNSUBSCRIBE / disconnect walk
    // (unsubscribeChannelAcrossRegistries, dtor drain) iterated them. Pruning here — the only
    // mutator that scans the whole list — keeps it bounded by the live conn-pool count.
    bool already_tracked = false;
    for (auto it = subscription_registries_.begin(); it != subscription_registries_.end();) {
      auto existing = it->lock();
      if (existing == nullptr) {
        it = subscription_registries_.erase(it);
        continue;
      }
      if (existing == registry) {
        already_tracked = true;
      }
      ++it;
    }
    if (!already_tracked) {
      subscription_registries_.push_back(registry);
    }
  }
  SubscriptionRegistryPtr subscriptionRegistryForChannel(const std::string& channel) override {
    auto it = subscription_registry_by_channel_.find(channel);
    if (it == subscription_registry_by_channel_.end()) {
      return nullptr;
    }
    auto registry = it->second.lock();
    if (registry == nullptr) {
      // Owner registry was torn down (cluster update) without going through unbind — prune the dead
      // entry so the caller falls back to the current route.
      subscription_registry_by_channel_.erase(it);
    }
    return registry;
  }
  void bindSubscriptionRegistryForChannel(const std::string& channel,
                                          const SubscriptionRegistryPtr& registry) override {
    // Track for the disconnect / cross-registry UNSUBSCRIBE walk (idempotent), then pin this
    // channel's owner so a later duplicate SUBSCRIBE routes back here rather than to whatever the
    // current route now resolves.
    setSubscriptionRegistry(registry);
    subscription_registry_by_channel_[channel] = registry;
  }
  void unbindSubscriptionRegistryForChannel(const std::string& channel) override {
    subscription_registry_by_channel_.erase(channel);
  }
  uint64_t unsubscribeChannelAcrossRegistries(
      const std::string& channel, const DownstreamSubscriberPtr& subscriber,
      std::vector<Common::Redis::RespValue>& preserved_acks) override {
    // Client-facing UNSUBSCRIBE drives the sharded path (the matching SUBSCRIBE was rewritten to
    // SSUBSCRIBE), so only subscriptions_/subscribed_channels_ carry state to clean up. A
    // still-pending ``subscribe`` ack for the channel is completed at its parked SUBSCRIBE
    // request's FIFO slot (posted by the registry) when that request is still live; only when it is
    // not does ``preserved_acks`` buffer the ack for the splitter to flush after its terminal
    // respond(), never mid-teardown (reentrancy/UAF). Owner-first: a channel is bound to exactly
    // one registry on subscribe (bindSubscriptionRegistryForChannel), so the common path
    // unsubscribes there directly with no scan, and drops the binding once the subscriber no longer
    // holds the channel anywhere.
    if (auto owner = subscriptionRegistryForChannel(channel)) {
      const uint64_t count =
          owner->unsubscribe(absl::MakeConstSpan(&channel, 1), subscriber, &preserved_acks);
      if (!subscriber->subscribedChannels().contains(channel)) {
        unbindSubscriptionRegistryForChannel(channel);
      }
      return count;
    }
    // Fallback with no binding (channel subscribed before ownership tracking, or the owner registry
    // expired): sweep EVERY tracked registry and clean up in all of them — do NOT break at the
    // first drop. A channel normally lives in one registry, but were an earlier defect to duplicate
    // it across registries, an early break would strand the copies; a full sweep converges the
    // state. The per-subscriber count only really moves in the one owning registry, so the
    // subscriber's post-sweep total is the correct downstream ack count.
    for (const auto& weak_reg : subscription_registries_) {
      auto reg = weak_reg.lock();
      if (!reg) {
        continue;
      }
      reg->unsubscribe(absl::MakeConstSpan(&channel, 1), subscriber, &preserved_acks);
    }
    return subscriber->totalSubscriptionCount();
  }
  void onPubsubSubscriptionChange(int64_t delta) override {
    // Cumulative subscribe/unsubscribe event counters only. The pubsub_active_subscriptions GAUGE
    // is owned by DownstreamSubscriber::addChannel/removeChannel and must NOT be touched here
    // — doing both would double-count.
    if (delta > 0) {
      config_->stats_.pubsub_subscribe_total_.add(delta);
    } else if (delta < 0) {
      config_->stats_.pubsub_unsubscribe_total_.add(-delta);
    }
  }
  // Order a MESSAGE push behind in-flight command replies (FIFO). With an empty FIFO the push goes
  // straight to the wire in arrival order; otherwise it is parked on the most recently queued
  // request and flushed right after that request's reply (so it never overtakes a reply that
  // preceded it, nor waits on requests that followed it). Parked bytes are bounded by the
  // connection buffer limit — the same limit the slow-subscriber high-watermark path uses — so a
  // FIFO that never drains evicts the subscriber instead of buffering without limit. Invoked via
  // ProxyFilterPushSink from DownstreamSubscriber::deliverMessage.
  //
  // ``encoded`` is MOVED out (no copy), left empty on return: every caller (deliverMessage,
  // deliverSharedFrame) owns a throwaway encode buffer, so there is a single ownership-transfer
  // overload — the former copying const& overload was dropped once fan-out got its own
  // per-subscriber buffers.
  void enqueueOrderedPush(Buffer::Instance& encoded);
  // Evict a slow pub/sub subscriber: mark it closed (idempotency guard is the caller's), bump
  // ``pubsub_slow_subscriber_closed``, log ``reason``, and close the connection (NoFlush). Shared
  // by the two backpressure triggers — the connection write-buffer high watermark and the
  // parked-push byte bound.
  void closeSlowSubscriber(const std::string& reason);

  Common::Redis::DecoderPtr decoder_;
  Common::Redis::EncoderPtr encoder_;
  CommandSplitter::Instance& splitter_;
  ProxyFilterConfigSharedPtr config_;
  Buffer::OwnedImpl encoder_buffer_;
  Network::ReadFilterCallbacks* callbacks_{};
  // RESP3 permits subscribed clients to continue issuing ordinary commands. Ordinary command
  // replies, subscribe / unsubscribe acks, and inline pub/sub ``-ERR``s ALL flow through
  // pending_requests_ (the FIFO): each is a frame on its own request, so it flushes at that
  // request's position and never overtakes an earlier pipelined reply. A fresh SUBSCRIBE holds its
  // FIFO entry until every one of its channels' upstream SSUBSCRIBE acks lands, then respond()s
  // them in command order — so even a deferred subscribe ack is ordered exactly where a single
  // serial Redis connection would place it (a command pipelined behind a slow subscribe waits for
  // its reply, just as it would behind any other in-flight command). MESSAGE Push frames are the
  // one exception: they carry no request/reply matching, so they are ordered separately via
  // DownstreamSubscriber::enqueueOrderedPush, which parks each behind any in-flight command reply
  // so a self-publish cannot overtake the publisher's own reply.
  std::list<PendingRequest> pending_requests_;
  bool connection_allowed_;
  // Per-connection negotiated downstream RESP version, held as the wire integer (2 or 3) so
  // it compares directly against the ``HELLO N`` argument. This is distinct from the
  // listener-policy type ``Common::Redis::RespProtocolVersion`` returned by
  // ``ProxyFilterConfig::protocolVersion()``; the two are bridged by ``toWireRespVersion`` /
  // ``toRespProtocolVersion`` at the boundaries. Starts at 2 (a legacy client never sends
  // HELLO) and is flipped by ``setDownstreamRespVersion`` when a ``HELLO N`` whose ``N``
  // matches the listener policy succeeds.
  uint32_t downstream_resp_version_{2};
  bool connection_quit_;
  // True while resumeAuthHeldRequests is draining. A resumed AUTH that resolves synchronously
  // re-enters resumeAuthHeldRequests via onAuthenticateExternal; the nested call must not start
  // a second drain loop over the same list.
  bool resuming_held_requests_{false};
  // Set once we've closed this connection as a slow pub/sub subscriber, so the (re-firing) high
  // watermark callback closes it exactly once.
  bool slow_subscriber_closed_{false};
  ExternalAuth::ExternalAuthClientPtr auth_client_;
  ExternalAuthCallStatus external_auth_call_status_;
  long external_auth_expiration_epoch_;
  DownstreamSubscriberPtr subscriber_;
  // Push-ordering sink handed (weakly) to subscriber_ so its MESSAGE pushes route back through
  // enqueueOrderedPush. This filter owns the only shared_ptr and detaches it in the destructor, so
  // a subscriber that briefly outlives the filter degrades to a direct write instead of a UAF.
  std::shared_ptr<ProxyFilterPushSink> push_sink_;
  // Running total of the bytes parked across pending_requests_' trailing_pushes_ buffers. Bounded
  // against the connection buffer limit for pub/sub backpressure (see enqueueOrderedPush); kept in
  // sync as pushes are parked (enqueueOrderedPush) and flushed (flushReadyResponses) and reset when
  // the FIFO is torn down (onEvent).
  uint64_t held_push_bytes_{0};
  std::vector<std::weak_ptr<SubscriptionRegistry>>
      subscription_registries_; // Weak refs, from conn pools.
  // Per-channel owning registry (weak refs), enforcing "one channel -> one registry per
  // connection". Bound on first SUBSCRIBE, consulted before route resolution on every SUBSCRIBE /
  // UNSUBSCRIBE, and erased when the subscriber no longer holds the channel. Weak so a registry a
  // cluster update tears down does not linger; a lapsed entry is pruned on lookup
  // (subscriptionRegistryForChannel).
  absl::flat_hash_map<std::string, std::weak_ptr<SubscriptionRegistry>>
      subscription_registry_by_channel_;
  // Declared LAST (R8-N4) so it is destroyed FIRST. ProxyFilter is registered as the connection
  // callback of the transaction client's UPSTREAM connection via upstream_transaction_cb_
  // (transaction_ holds &upstream_transaction_cb_ as its connection_cb_). If ~Transaction ever
  // closes that connection synchronously it re-enters handleConnectionEvent(is_downstream=false),
  // which reads external_auth_call_status_ / auth_client_. Members are destroyed in reverse
  // declaration order, so declaring transaction_ last guarantees every member that re-entry can
  // touch is still alive. upstream_transaction_cb_ precedes transaction_ (constructed first,
  // destroyed last) because transaction_ dereferences it during that very re-entry. The normal
  // teardown already closes the transaction before ~ProxyFilter (making the dtor a no-op); this
  // layout makes safety a property of the declaration order, not of that external invariant.
  UpstreamTransactionCallbacks upstream_transaction_cb_{*this};
  Common::Redis::Client::Transaction transaction_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
