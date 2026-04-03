#include <memory>
#include <string>
#include <vector>

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/network/redis_proxy/subscription_registry.h"

#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/extensions/filters/network/redis_proxy/pubsub_test_utils.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

// Convenience alias for the SUNSUBSCRIBE outcome enum used throughout these tests.
using SunsubscribeResult = UpstreamSubscriptionCallbacks::SunsubscribeResult;

// Test-only multi-channel subscribe. Production drives ONE channel per call (the splitter), so this
// used to be SubscriptionRegistry::subscribeChannels — a public method retained only for tests.
// Moved out of the production interface (§6): it needs no registry internals, just the public
// single-channel subscribe() looped per channel. Callers ignore the per-call result.
void subscribeChannels(SubscriptionRegistry& registry, absl::Span<const std::string> channels,
                       const DownstreamSubscriberPtr& subscriber) {
  for (const auto& channel : channels) {
    registry.subscribe(channel, subscriber);
  }
}

class SubscriptionRegistryTest : public testing::Test {
protected:
  SubscriptionRegistryTest()
      : registry_(upstream_callbacks_, random_, dispatcher_),
        mock_host_(std::make_shared<NiceMock<Upstream::MockHost>>()),
        mock_host_2_(std::make_shared<NiceMock<Upstream::MockHost>>()) {
    // The topology-change reroute now sends via sendUpstreamSsubscribeToHost with the host it
    // already dry-resolved (E-8), rather than re-resolving inside sendUpstreamSsubscribe. Default
    // it to a successful send so tests that only care about the SUNSUBSCRIBE/ack bookkeeping around
    // a reroute need no per-test wiring; the reroute still records the dry-resolved host as the
    // channel's new owner via recordSsubscribeAttempt. Tests asserting a FAILED reroute override.
    ON_CALL(upstream_callbacks_, sendUpstreamSsubscribeToHost(_, _, _)).WillByDefault(Return(true));
    // Model the real conn pool's retire decision (ALT-1): retireSubscriptionConnectionIfIdle
    // retires — and reports true, telling the registry to drop the host's control ledger — exactly
    // when the host has no remaining subscriptions (the pool's own hostHasSubscriptions check). The
    // registry clears the ledger off this REPORTED decision rather than re-deriving the predicate,
    // so tests that assert the retire-drops-ledger behavior get the clear via this default; tests
    // that only assert the call happens (EXPECT_CALL(retire...)) inherit it too.
    ON_CALL(upstream_callbacks_, retireSubscriptionConnectionIfIdle(_))
        .WillByDefault(Invoke([this](const Upstream::HostConstSharedPtr& host) {
          return !registry_.hostHasSubscriptions(host);
        }));
    // The registry now takes a mandatory dispatcher (S-4) and lazily creates its subscribe-ack and
    // resubscribe-generation timers. Hand every createTimer_ a throwaway mock timer so the default
    // tests — which never fire these timers — exercise the same scheduling path production does
    // with no per-test wiring; the dedicated timeout tests below build their own dispatcher +
    // MockTimer to capture and fire the callback.
    ON_CALL(dispatcher_, createTimer_(_)).WillByDefault(Invoke([](Event::TimerCb) -> Event::Timer* {
      return new NiceMock<Event::MockTimer>();
    }));
  }

  // Action that returns mock_host_ as the chosen shard from ``sendUpstreamSsubscribe`` (a non-null
  // return is the channel's owner; nullptr would be a send failure).
  auto ssubscribeReturnsHost() {
    return [this](const std::string&,
                  Common::Redis::Client::PushMessageCallbacks&) -> Upstream::HostConstSharedPtr {
      return mock_host_;
    };
  }
  // Same, but selects the SECOND host (a channel that moved A -> B).
  auto ssubscribeReturnsHost2() {
    return [this](const std::string&,
                  Common::Redis::Client::PushMessageCallbacks&) -> Upstream::HostConstSharedPtr {
      return mock_host_2_;
    };
  }

  DownstreamSubscriberPtr makeSubscriber() {
    auto connection = std::make_unique<NiceMock<Network::MockConnection>>();
    auto* raw = connection.get();
    owned_connections_.push_back(std::move(connection));
    connections_.push_back(raw);
    return std::make_shared<DownstreamSubscriber>(*raw, throwaway_stats_);
  }

  // Subscriber wired with the ack-time subscribe success/error counters (Issue 6); the push/gauge
  // stats it does not assert on come from the fixture's throwaway store (S-4 made all four
  // mandatory).
  DownstreamSubscriberPtr makeSubscriberWithAckCounters(Stats::Counter& success,
                                                        Stats::Counter& error) {
    auto connection = std::make_unique<NiceMock<Network::MockConnection>>();
    auto* raw = connection.get();
    owned_connections_.push_back(std::move(connection));
    connections_.push_back(raw);
    return std::make_shared<DownstreamSubscriber>(
        *raw, DownstreamSubscriberStats{throwaway_push_, throwaway_gauge_, success, error});
  }

  NiceMock<Envoy::Random::MockRandomGenerator> random_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockUpstreamSubscriptionCallbacks> upstream_callbacks_;
  // Throwaway pub/sub stats for subscribers whose telemetry a test does not assert on (S-4 made the
  // four DownstreamSubscriber stats mandatory). Tests that DO assert on the gauge / ack counters
  // build their own DownstreamSubscriberStats with the stat under assertion. These MUST be declared
  // before ``registry_``: the registry holds its subscribers by strong ref (SubsMap), so their
  // ~DownstreamSubscriber — which drains the active-subscriptions gauge — runs during registry_
  // destruction, and the gauge (and its store) must still be alive at that point.
  Stats::IsolatedStoreImpl stats_store_;
  Stats::Counter& throwaway_push_{stats_store_.rootScope()->counterFromString("test.pubsub.push")};
  Stats::Gauge& throwaway_gauge_{stats_store_.rootScope()->gaugeFromString(
      "test.pubsub.active", Stats::Gauge::ImportMode::Accumulate)};
  Stats::Counter& throwaway_ack_ok_{
      stats_store_.rootScope()->counterFromString("test.pubsub.ack_ok")};
  Stats::Counter& throwaway_ack_err_{
      stats_store_.rootScope()->counterFromString("test.pubsub.ack_err")};
  DownstreamSubscriberStats throwaway_stats_{throwaway_push_, throwaway_gauge_, throwaway_ack_ok_,
                                             throwaway_ack_err_};
  SubscriptionRegistry registry_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> mock_host_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> mock_host_2_;
  std::vector<std::unique_ptr<NiceMock<Network::MockConnection>>> owned_connections_;
  std::vector<NiceMock<Network::MockConnection>*> connections_;
};

// --- ``SSUBSCRIBE`` routes each channel to its slot owner and sends upstream ---

TEST_F(SubscriptionRegistryTest, SsubscribeSendsUpstream) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("sharded-ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));

  uint64_t count = registry_.subscribe({"sharded-ch"}, sub).subscription_count;
  EXPECT_EQ(1, count);
  EXPECT_EQ(1, registry_.subscriptionCount());
}

TEST_F(SubscriptionRegistryTest, SsubscribeSecondSubscriberNoUpstream) {
  auto sub1 = makeSubscriber();
  auto sub2 = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("events", _))
      .Times(1)
      .WillOnce(Invoke(ssubscribeReturnsHost()));

  registry_.subscribe({"events"}, sub1);
  registry_.subscribe({"events"}, sub2); // No upstream call — already subscribed.
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// --- Unsubscribe sends upstream ``SUNSUBSCRIBE`` ---

TEST_F(SubscriptionRegistryTest, SunsubscribeLastSendsUpstream) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch1"}, sub);

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch1", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.unsubscribe({"ch1"}, sub);
  EXPECT_EQ(0, registry_.subscriptionCount());
}

TEST_F(SubscriptionRegistryTest, SunsubscribeOneOfTwoDoesNotSendUpstream) {
  auto sub1 = makeSubscriber();
  auto sub2 = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("shared", _))
      .Times(1)
      .WillOnce(Invoke(ssubscribeReturnsHost()));

  registry_.subscribe({"shared"}, sub1);
  registry_.subscribe({"shared"}, sub2);

  // Unsubscribing one of two — no upstream ``SUNSUBSCRIBE``.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe(_, _)).Times(0);
  registry_.unsubscribe({"shared"}, sub1);
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// P3 regression: unsubscribing a channel on a registry that does NOT own it (a multi-cluster
// subscriber whose UNSUBSCRIBE walks every tracked registry) must not touch the subscriber's shared
// channel set — otherwise the owning registry is skipped (the splitter stops once the count drops)
// and the real subscription is stranded with no upstream SUNSUBSCRIBE.
TEST_F(SubscriptionRegistryTest, UnsubscribeOnNonOwningRegistryLeavesSharedSetIntact) {
  NiceMock<MockUpstreamSubscriptionCallbacks> other_callbacks;
  SubscriptionRegistry other_registry(other_callbacks, random_, dispatcher_);
  auto sub = makeSubscriber();

  // The subscriber owns "ch" on registry_ (the owning registry).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  EXPECT_EQ(1, sub->totalSubscriptionCount());

  // Unsubscribe "ch" on the OTHER registry, which never had it: no SUNSUBSCRIBE fires there, and
  // the subscriber's shared set is left intact (still owns "ch" on registry_ — not stranded).
  EXPECT_CALL(other_callbacks, sendUpstreamSunsubscribe(_, _)).Times(0);
  other_registry.unsubscribe({"ch"}, sub);
  EXPECT_EQ(1, sub->totalSubscriptionCount());
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// --- Fan-out delivery ---

TEST_F(SubscriptionRegistryTest, FanOutToMultipleSubscribers) {
  auto sub1 = makeSubscriber();
  auto sub2 = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("events", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));

  registry_.subscribe({"events"}, sub1);
  registry_.subscribe({"events"}, sub2);

  // Two subscribers → the fan-out encodes the frame ONCE and hands the same bytes to each. Assert
  // the exact wire bytes on both connections, not just that a write happened, so a regression in
  // the encode-once path (deliverEncoded / the shared-buffer copy) that corrupts or drops bytes is
  // caught here rather than silently shipping a malformed Push to half the subscribers. The
  // upstream ``smessage`` is normalized to ``message`` on the wire.
  const std::string expected = ">3\r\n$7\r\nmessage\r\n$6\r\nevents\r\n$7\r\npayload\r\n";
  EXPECT_CALL(*connections_[0], write(BufferString(expected), false));
  EXPECT_CALL(*connections_[1], write(BufferString(expected), false));
  registry_.onPushMessage(makeSmessagePush("events", "payload"), mock_host_);
}

// --- Disconnect cleanup sends upstream ``SUNSUBSCRIBE`` ---

TEST_F(SubscriptionRegistryTest, RemoveSubscriberSendsUpstreamUnsubscribe) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));

  subscribeChannels(registry_, {"ch1", "ch2"}, sub);

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch1", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch2", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));

  registry_.removeSubscriber(sub);

  EXPECT_EQ(0, registry_.subscriptionCount());
}

TEST_F(SubscriptionRegistryTest, RemoveSubscriberLeavesOthers) {
  auto sub1 = makeSubscriber();
  auto sub2 = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("shared", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));

  registry_.subscribe({"shared"}, sub1);
  registry_.subscribe({"shared"}, sub2);

  // Removing sub1 — sub2 still active, no upstream ``SUNSUBSCRIBE``.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe(_, _)).Times(0);
  registry_.removeSubscriber(sub1);
  EXPECT_EQ(1, registry_.subscriptionCount());

  // sub2 still receives messages.
  EXPECT_CALL(*connections_[1], write(_, _));
  registry_.onPushMessage(makeSmessagePush("shared", "hello"), mock_host_);
}

// --- Upstream connection loss triggers re-subscribe ---

TEST_F(SubscriptionRegistryTest, UpstreamConnectionCloseResubscribesWithBackoff) {
  auto sub1 = makeSubscriber();
  auto sub2 = makeSubscriber();

  // Initial subscribe: sub1 owns ch1+ch2, sub2 joins ch1 (dedup, no extra send).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));

  subscribeChannels(registry_, {"ch1", "ch2"}, sub1);
  registry_.subscribe({"ch1"}, sub2); // Dedup on ch1, no upstream send.

  // Upstream connection lost — scheduleResubscribe is called with backoff (the pool would arm a
  // timer that later drives registry_.doResubscribe()).
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));

  registry_.onUpstreamConnectionClose();

  // Drive the deferred re-subscribe (what the pool's timer does) — re-issues each sharded channel.
  registry_.doResubscribe();

  // After re-subscribe, fan-out should still work.
  EXPECT_CALL(*connections_[0], write(_, _));
  EXPECT_CALL(*connections_[1], write(_, _));
  registry_.onPushMessage(makeSmessagePush("ch1", "after-reconnect"), mock_host_);
}

TEST_F(SubscriptionRegistryTest, ResubscribeRetriesWhenUpstreamSendFails) {
  auto sub = makeSubscriber();

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onUpstreamConnectionClose();

  // First re-subscribe attempt: the upstream send fails (nullptr host) → registry reschedules.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _)).WillOnce(Return(nullptr));
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.doResubscribe();

  // Second attempt driven by the rescheduled timer: succeeds.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.doResubscribe();

  EXPECT_CALL(*connections_[0], write(_, _));
  registry_.onPushMessage(makeSmessagePush("ch", "after-retry"), mock_host_);
}

TEST_F(SubscriptionRegistryTest, UpstreamConnectionCloseNoSubscriptions) {
  // No subscriptions active — onUpstreamConnectionClose is a no-op.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_)).Times(0);
  registry_.onUpstreamConnectionClose();
}

// --- Edge cases ---

TEST_F(SubscriptionRegistryTest, PushToUnknownChannel) {
  EXPECT_NO_THROW(registry_.onPushMessage(makeSmessagePush("unknown", "data"), mock_host_));
}

TEST_F(SubscriptionRegistryTest, NullPushMessage) {
  EXPECT_NO_THROW(registry_.onPushMessage(nullptr, mock_host_));
}

TEST_F(SubscriptionRegistryTest, NonPushTypeMessage) {
  auto value = std::make_unique<Common::Redis::RespValue>();
  value->type(Common::Redis::RespType::SimpleString);
  value->asString() = "unexpected";
  EXPECT_NO_THROW(registry_.onPushMessage(std::move(value), mock_host_));
}

TEST_F(SubscriptionRegistryTest, EmptyPushMessage) {
  auto value = std::make_unique<Common::Redis::RespValue>();
  value->type(Common::Redis::RespType::Push);
  EXPECT_NO_THROW(registry_.onPushMessage(std::move(value), mock_host_));
}

TEST_F(SubscriptionRegistryTest, SunsubscribeNonExistent) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe(_, _)).Times(0);
  uint64_t count = registry_.unsubscribe({"nonexistent"}, sub);
  EXPECT_EQ(0, count);
}

// --- Sharded ``smessage`` fan-out ---

TEST_F(SubscriptionRegistryTest, SmessageFanOut) {
  auto sub1 = makeSubscriber();
  auto sub2 = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("sch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));

  registry_.subscribe({"sch"}, sub1);
  registry_.subscribe({"sch"}, sub2);

  EXPECT_CALL(*connections_[0], write(_, _));
  EXPECT_CALL(*connections_[1], write(_, _));
  registry_.onPushMessage(makeSmessagePush("sch", "sharded-data"), mock_host_);
}

// Sharded ``smessage`` push frames must be normalized to ``message`` before delivery —
// the splitter never exposes ``SUBSCRIBE`` clients to the sharded verb, so the
// downstream wire shape for the rewritten path must read ``message`` not ``smessage``.
TEST_F(SubscriptionRegistryTest, SmessageDeliveredAsMessageDownstream) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("sch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"sch"}, sub);

  // RESP3 Push (``>3``) → ``$7\r\nmessage`` (the rewritten type) → ``$3\r\nsch`` →
  // ``$5\r\nhello``. ``$8\r\nsmessage`` must NOT appear.
  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$7\r\nmessage\r\n$3\r\nsch\r\n$5\r\nhello\r\n"), false));
  registry_.onPushMessage(makeSmessagePush("sch", "hello"), mock_host_);
}

// The registry rewrites a client SUBSCRIBE into an upstream SSUBSCRIBE and parks a pending ack.
// When the upstream ssubscribe-ack push arrives, the registry must emit the literal client verb
// ``subscribe`` downstream — NOT the upstream ``ssubscribe`` verb — so a client that sent
// SUBSCRIBE never sees the sharded variant.
TEST_F(SubscriptionRegistryTest, PendingAckEmitsSubscribeVerbOnSsubscribeAck) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  // Downstream sees ``subscribe`` (the literal client verb) even though the upstream ack frame's
  // type is ``ssubscribe`` — the canary that smashes the entire rewrite if it regresses. The
  // upstream-echoed count is ignored in favor of the subscriber's snapshot count (1).
  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$2\r\nch\r\n:1\r\n"), false));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
}

// A multi-channel subscribe must stamp each pending ack with the subscriber's running count as of
// that channel (a→1, b→2), not one end-of-batch snapshot shared by both. The splitter drives one
// channel per call, but the registry's running-step count keeps a direct multi-channel subscribe()
// correct too. The upstream ack's own echoed count (99) is intentionally ignored — the downstream
// ack always carries the snapshot recorded at subscribe-call time. Regression for C-8.
TEST_F(SubscriptionRegistryTest, SubscribeAcksCarryPerChannelStepCount) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  subscribeChannels(registry_, {"a", "b"}, sub);

  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$1\r\na\r\n:1\r\n"), false));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 99), mock_host_);

  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$1\r\nb\r\n:2\r\n"), false));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "b", 99), mock_host_);
}

// Issue 4 (round-4/5): a client that pipelines SUBSCRIBE foo then UNSUBSCRIBE foo before the
// upstream ssubscribe ack lands must still receive the ``subscribe`` ack — Redis replies
// ``subscribe foo 1`` then ``unsubscribe foo 0``. The registry PRESERVES it by APPENDING to the
// caller's buffer — it does NOT write downstream (F1: the splitter flushes it after its terminal
// respond(), reentrancy-safe) — and drains the pending bucket so a late upstream ack yields no
// duplicate.
TEST_F(SubscriptionRegistryTest, UnsubscribeBeforeAckPreservesSubscribeAck) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("foo", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"foo"}, sub); // pending: ssubscribe sent, no downstream ack yet.

  // Unsubscribing while still pending must NOT write downstream from inside the registry; it
  // collects the preserved ``subscribe foo 1`` into the caller's buffer and issues the upstream
  // SUNSUBSCRIBE for the now-orphaned channel. The returned count (0) drives the splitter's
  // ``unsubscribe foo 0``.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("foo", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  std::vector<Common::Redis::RespValue> preserved;
  EXPECT_EQ(0, registry_.unsubscribe({"foo"}, sub, &preserved));

  // Exactly one preserved ack, the literal ``subscribe`` verb carrying the snapshot count (1).
  ASSERT_EQ(1, preserved.size());
  EXPECT_EQ("subscribe", preserved[0].asArray()[0].asString());
  EXPECT_EQ("foo", preserved[0].asArray()[1].asString());
  EXPECT_EQ(1, preserved[0].asArray()[2].asInteger());

  // The pending bucket is drained: a late upstream ssubscribe ack delivers no duplicate (still no
  // write, asserted above for the whole test).
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "foo", 1), mock_host_);
}

// --- Cluster topology change re-routes sharded channels ---

TEST_F(SubscriptionRegistryTest, TopologyChangeReroutesSharded) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("s1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("s2", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));

  registry_.subscribe({"s1"}, sub);
  registry_.subscribe({"s2"}, sub);

  // Topology change: each sharded channel ``SUNSUBSCRIBE``s the old host and ``SSUBSCRIBE``s the
  // new owner (no wait for the unsubscribe ack — favors continuity over a strict no-duplicate
  // window).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("s1", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("s2", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  // The reroute SSUBSCRIBEs the new owner via sendUpstreamSsubscribeToHost, handed the host the
  // registry already dry-resolved (mock_host_2_) rather than re-resolving in the send (E-8).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribeToHost("s1", _, _)).WillOnce(Return(true));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribeToHost("s2", _, _)).WillOnce(Return(true));
  // The reroute fires only on a genuine owner change (G4): the dry-resolve must return a host
  // different from the current owner (mock_host_). A null / unchanged resolve now correctly leaves
  // the channel in place.
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("s1"))
      .WillRepeatedly(Return(mock_host_2_));
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("s2"))
      .WillRepeatedly(Return(mock_host_2_));
  registry_.onClusterTopologyChange();
}

// --- Per-host subscription index (hostHasSubscriptions) ---

// The registry maintains an O(1) per-host channel count so the conn pool can decide whether a
// host's dedicated subscription connection is now idle without scanning the whole channel->host
// map (which made mass unsubscribe O(n^2)). Verify the count tracks subscribe/unsubscribe and drops
// the host once its last channel goes.
TEST_F(SubscriptionRegistryTest, HostHasSubscriptionsTracksSubscribeAndUnsubscribe) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost())); // every channel -> mock_host_

  EXPECT_FALSE(registry_.hostHasSubscriptions(mock_host_));

  registry_.subscribe({"a"}, sub);
  registry_.subscribe({"b"}, sub);
  EXPECT_TRUE(registry_.hostHasSubscriptions(mock_host_));

  // One of two channels gone — the host still serves the other.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("a", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.unsubscribe({"a"}, sub);
  EXPECT_TRUE(registry_.hostHasSubscriptions(mock_host_));

  // Last channel gone — the host drops out of the index.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("b", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.unsubscribe({"b"}, sub);
  EXPECT_FALSE(registry_.hostHasSubscriptions(mock_host_));
}

// dropHost (the cluster removed the host) clears it from the index in one shot.
TEST_F(SubscriptionRegistryTest, DropHostClearsHostSubscriptionIndex) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  subscribeChannels(registry_, {"a", "b"}, sub);
  EXPECT_TRUE(registry_.hostHasSubscriptions(mock_host_));

  registry_.dropHost(mock_host_);
  EXPECT_FALSE(registry_.hostHasSubscriptions(mock_host_));
}

// --- Unsolicited upstream SUNSUBSCRIBE (slot migration) ---

// When a channel's slot migrates off this owner, Redis pushes an unsolicited SUNSUBSCRIBE
// (pubsubShardUnsubscribeAllChannelsInSlot). The channel is STILL locally subscribed, so this is
// NOT the advisory ack to one of our own SUNSUBSCRIBEs: forget the now-stale owner (mark for
// re-resolution) but keep the subscription. Actual re-subscribe is driven later by the slot-map
// refresh (onClusterTopologyChange); here a subsequent doResubscribe re-issues it.
TEST_F(SubscriptionRegistryTest, UpstreamUnsolicitedSunsubscribeForActiveChannelMarksForReresolve) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .Times(2)
      .WillRepeatedly(
          Invoke(ssubscribeReturnsHost())); // initial subscribe + doResubscribe re-issue
  registry_.subscribe({"ch"}, sub);
  // Deliver the upstream ack so this is a real ACTIVE subscription, not merely pending.
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));
  EXPECT_TRUE(registry_.hostHasSubscriptions(mock_host_));

  // Unsolicited SUNSUBSCRIBE: stale owner forgotten, subscription kept, no immediate re-subscribe.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(0, registry_.channelHosts().count("ch"));
  EXPECT_FALSE(registry_.hostHasSubscriptions(mock_host_));
  EXPECT_EQ(1, registry_.subscriptionCount());

  // The topology-driven resubscribe re-resolves and re-SSUBSCRIBEs the channel (the 2nd send
  // above).
  registry_.doResubscribe();
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));
}

// A SUNSUBSCRIBE for a channel with no live local subscription is the advisory ack to our own
// upstream SUNSUBSCRIBE (sent when a channel's last subscriber left) — a harmless no-op.
TEST_F(SubscriptionRegistryTest, UpstreamSunsubscribeAckForInactiveChannelIsAdvisoryNoop) {
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _)).Times(0);
  EXPECT_NO_THROW(
      registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ghost", 0), mock_host_));
  EXPECT_EQ(0, registry_.subscriptionCount());
}

// Two channels share one owner; only one channel's slot moves. Forgetting that channel's host must
// decrement — not zero — the per-host count, so the host stays in the index for its other channel.
TEST_F(SubscriptionRegistryTest, UnsolicitedSunsubscribeDecrementsButKeepsHostWithOtherChannel) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost())); // "a" and "b" both -> mock_host_
  subscribeChannels(registry_, {"a", "b"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 1), mock_host_);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "b", 2), mock_host_);
  EXPECT_TRUE(registry_.hostHasSubscriptions(mock_host_));

  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "a", 0), mock_host_);
  EXPECT_EQ(0, registry_.channelHosts().count("a"));       // "a" forgotten
  EXPECT_EQ(1, registry_.channelHosts().count("b"));       // "b" untouched
  EXPECT_TRUE(registry_.hostHasSubscriptions(mock_host_)); // host still serves "b"
  EXPECT_EQ(2, registry_.subscriptionCount());
}

// Blocker regression: a topology change SUNSUBSCRIBEs the old host (leaving an expected ack) and
// re-records the channel on the new owner. When the old host's SUNSUBSCRIBE ack arrives the channel
// is still active — but this is OUR advisory ack, not an unsolicited invalidation, so the
// freshly-set host mapping must be preserved (expected-ack tracking distinguishes them).
TEST_F(SubscriptionRegistryTest, TopologyChangeSunsubscribeAckDoesNotForgetNewHost) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  // Ack the initial SSUBSCRIBE so it leaves mock_host_'s control FIFO (Redis acks in order),
  // leaving the topology reroute's SUNSUBSCRIBE at the head when its advisory ack arrives below
  // (A-4/S-1).
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  // Genuine owner change so the reroute fires (G4: a null resolve now leaves the channel put).
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillRepeatedly(Return(mock_host_2_));
  registry_.onClusterTopologyChange(); // SUNSUBSCRIBE old (expected ack) + SSUBSCRIBE new owner
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // The advisory ack comes FROM the old owner (where the SUNSUBSCRIBE was sent); its control-FIFO
  // head is that sunsubscribe, so it is consumed as advisory and the new mapping is kept.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // NOT forgotten
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// Blocker regression: last subscriber UNSUBSCRIBEs (upstream SUNSUBSCRIBE, expected ack), then the
// channel is re-subscribed before the ack arrives (host re-recorded). The stale ack is OURS
// (advisory) and must not forget the re-set mapping.
TEST_F(SubscriptionRegistryTest, SunsubscribeAckAfterRapidResubscribeDoesNotForgetHost) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  // Ack the initial SSUBSCRIBE so it clears mock_host_'s control FIFO (Redis acks in order); the
  // UNSUBSCRIBE's SUNSUBSCRIBE below is then the FIFO head when its stale advisory ack arrives.
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.unsubscribe({"ch"}, sub);
  EXPECT_EQ(0, registry_.subscriptionCount());
  registry_.subscribe({"ch"}, sub); // rapid re-subscribe, host re-recorded
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // Stale advisory ack from mock_host_ (where the SUNSUBSCRIBE went): its control-FIFO head is that
  // sunsubscribe, so it is consumed as OUR ack — the re-set mapping is kept, not invalidated.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// Blocker regression: an expected SUNSUBSCRIBE ack must not go stale. If the host we sent it to
// drops before acking, the expectation is cleared — so a LATER genuine unsolicited sunsubscribe
// (slot moved elsewhere) is correctly treated as an invalidation, not swallowed as the unacked
// expectation.
TEST_F(SubscriptionRegistryTest, SunsubscribeExpectedAckClearedWhenHostConnectionCloses) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  // Genuine owner change so the reroute fires (G4: a null resolve now leaves the channel put).
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillRepeatedly(Return(mock_host_2_));
  registry_.onClusterTopologyChange(); // leaves an expected SUNSUBSCRIBE ack for mock_host_

  registry_.onUpstreamConnectionClose(
      mock_host_); // host drops before acking -> expectation cleared

  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0),
                          mock_host_2_); // now genuinely unsolicited (from the new owner)
  EXPECT_EQ(0, registry_.channelHosts().count("ch")); // forgotten (re-resolution)
  EXPECT_EQ(1, registry_.subscriptionCount());        // still subscribed
}

// Blocker regression: a SUNSUBSCRIBE of a host's LAST channel makes the conn pool retire that
// host's dedicated subscription connection inline (maybeCleanupSubscriptionMode reports
// ConnectionRetired), and that planned retire deliberately skips onUpstreamConnectionClose. So the
// ack never comes back — recording (or keeping) an expectation for it would be stale, and a later
// genuine unsolicited SUNSUBSCRIBE (slot invalidation) would be wrongly swallowed by that stale
// entry, re-stranding the channel. On ConnectionRetired the registry must drop ALL of the host's
// expected acks — including one still pending for an EARLIER channel on the same host. Mirror of
// SunsubscribeExpectedAckClearedWhenHostConnectionCloses for the planned-retire path (which does
// not surface as a connection-close event).
TEST_F(SubscriptionRegistryTest, ConnectionRetiredSunsubscribeDropsHostExpectedAcks) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  subscribeChannels(registry_, {"a", "b"}, sub); // both on mock_host_
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 1), mock_host_);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "b", 2), mock_host_);

  // Unsubscribe "a" first: the host still carries "b", so the connection stays open and its
  // SUNSUBSCRIBE ack is expected (recorded).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("a", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.unsubscribe({"a"}, sub);

  // Unsubscribe "b" (the host's LAST channel): the conn pool retires the connection inline and
  // reports ConnectionRetired. The registry must drop ALL of the host's expected acks — including
  // the still-pending "a" ack — because the retired connection will never deliver them.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("b", _))
      .WillOnce(Return(SunsubscribeResult::ConnectionRetired));
  registry_.unsubscribe({"b"}, sub);
  EXPECT_EQ(0, registry_.subscriptionCount());

  // Re-subscribe "a"; it becomes active again on mock_host_.
  registry_.subscribe({"a"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 1), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("a"));

  // A genuine unsolicited SUNSUBSCRIBE for "a" (slot moved off the owner) must be treated as an
  // invalidation, NOT swallowed by the stale "a" ack that ConnectionRetired above dropped.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "a", 0), mock_host_);
  EXPECT_EQ(0, registry_.channelHosts().count("a")); // forgotten -> re-resolution (1 if swallowed)
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// G1 regression: the UNSOLICITED-invalidation retire path must drop the host's expected acks too,
// exactly like the ConnectionRetired send path above. When an unsolicited SUNSUBSCRIBE empties a
// host, the registry retires its (now idle) connection — and a SUNSUBSCRIBE ack still pending for
// an EARLIER channel on that host will never arrive, since the connection is gone. Leaving it
// recorded lets a later stale ack be swallowed, re-stranding a re-subscribed channel (same host-ack
// blackout as F2). This is the unsolicited-path mirror of
// ConnectionRetiredSunsubscribeDropsHostExpectedAcks.
TEST_F(SubscriptionRegistryTest, UnsolicitedSunsubscribeRetireDropsHostExpectedAcks) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  subscribeChannels(registry_, {"a", "b"}, sub); // both on mock_host_
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 1), mock_host_);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "b", 2), mock_host_);

  // Unsubscribe "a": the host still carries "b", so the connection stays open and "a"'s
  // SUNSUBSCRIBE ack is expected (recorded in the host's ledger).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("a", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.unsubscribe({"a"}, sub);

  // An unsolicited SUNSUBSCRIBE for "b" (its slot moved off mock_host_) forgets "b", emptying the
  // host; the registry retires its idle connection. That retire must ALSO drop the host's ledger —
  // including the still-pending "a" ack — because the retired connection will never deliver it.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "b", 0), mock_host_);

  // Re-subscribe "a"; it becomes active again on mock_host_.
  registry_.subscribe({"a"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 1), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("a"));

  // A genuine unsolicited SUNSUBSCRIBE for "a" must be an invalidation, NOT swallowed by the stale
  // "a" expected-ack the retire above should have dropped (count stays 1 if G1 leaked it).
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "a", 0), mock_host_);
  EXPECT_EQ(0, registry_.channelHosts().count("a"));
}

// A slot-only rebalance must NOT churn channels whose owner is unchanged. When the dry host-resolve
// returns the SAME host the channel is already on, onClusterTopologyChange leaves it untouched — no
// SUNSUBSCRIBE, no re-SSUBSCRIBE (which would waste upstream traffic and open a needless message
// gap / duplicate window on an unmoved channel).
TEST_F(SubscriptionRegistryTest, TopologyChangeSkipsChannelWithUnchangedOwner) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // only the initial subscribe should SSUBSCRIBE
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // Dry resolve returns the current owner: the channel did not move.
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch")).WillOnce(Return(mock_host_));
  // No SUNSUBSCRIBE (and, via the WillOnce above, no second SSUBSCRIBE): the subscription is left
  // in place.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe(_, _)).Times(0);
  registry_.onClusterTopologyChange();
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // still mapped to the same host, untouched
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// B1: an out-of-band control error (Redis's normal error reply to a fire-and-forget SSUBSCRIBE,
// e.g. -MOVED / -CROSSSLOT / -NOPERM) must NOT tear the shared subscription connection down, and it
// must KEEP this host's expected SUNSUBSCRIBE acks: the connection is still open and can still
// deliver them, so a subsequent SUNSUBSCRIBE ack is still OUR advisory ack (mapping preserved), not
// an unsolicited invalidation. Here the redirect correlates to a SUPERSEDED attempt (the channel
// already moved on), so S6-6 additionally ignores it rather than scheduling a redundant
// re-subscribe. This is the mirror of SunsubscribeExpectedAckClearedWhenHostConnectionCloses.
TEST_F(SubscriptionRegistryTest,
       UpstreamControlErrorForSupersededAttemptIsIgnoredButKeepsSunsubscribeAcks) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  // Genuine owner change so the reroute fires (G4: a null resolve now leaves the channel put).
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillRepeatedly(Return(mock_host_2_));
  registry_.onClusterTopologyChange(); // leaves an expected SUNSUBSCRIBE ack for mock_host_
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // Control error for the OLD attempt: the topology move above already re-resolved "ch" to
  // mock_host_2_ (generation 2), so this MOVED — whose FIFO head is the stale generation-1
  // SSUBSCRIBE still outstanding on mock_host_ — is for a SUPERSEDED attempt. S6-6 consumes that
  // FIFO head (so the sunsubscribe below is left at the head as our advisory ack) but does NOT
  // schedule a pointless re-subscribe of a channel that already moved (and is already pending on
  // mock_host_2_). The connection and its expected acks stay intact.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_)).Times(0);
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "MOVED 1234 127.0.0.1:6380";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // untouched by the control error

  // The expected ack survived: this SUNSUBSCRIBE ack (from mock_host_, where it was sent) is our
  // advisory ack — the MOVED error above popped the initial SSUBSCRIBE off the FIFO, leaving the
  // sunsubscribe at the head — so the mapping is kept (A-4/S-1).
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // NOT forgotten (contrast: close forgets it)
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// B2: a normal -ERR to a fire-and-forget SSUBSCRIBE is correlated (via the per-host outstanding-
// control FIFO recorded on send) to the channel that failed, so its pending downstream subscriber
// is failed IMMEDIATELY — connection closed (F3) and subscription rolled back — with no waiting for
// the subscribe-ack timeout.
TEST_F(SubscriptionRegistryTest, UpstreamSsubscribeErrorFailsPendingSubscriberImmediately) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub); // records {ssubscribe, "ch"} in mock_host_'s control FIFO
  EXPECT_EQ(1, registry_.subscriptionCount());

  // The correlated error fails the pending subscriber: instead of a desyncing out-of-band -ERR
  // (F3), the subscriber's connection is closed and the subscription is rolled back. No topology
  // refresh (not a redirect) and no host-scoped re-subscribe (the connection's other channels are
  // healthy).
  EXPECT_CALL(*connections_[0], close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_)).Times(0);
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "NOPERM this user has no permissions to run 'ssubscribe'";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
  EXPECT_EQ(0, registry_.subscriptionCount()); // rolled back
}

// B2: a -MOVED on a subscription connection means the local slot map is stale. Instead of a plain
// re-subscribe (which would just draw the same redirect), request a cluster topology refresh, then
// re-resolve this host's channels on backoff.
TEST_F(SubscriptionRegistryTest, UpstreamControlErrorMovedRequestsTopologyRefresh) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  EXPECT_CALL(upstream_callbacks_, requestTopologyRefresh());
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "MOVED 1234 127.0.0.1:6380";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
}

// Finding 2: an anomalous non-Error non-Push reply on a subscription connection still occupies the
// oldest outstanding control command's reply slot (Redis answers pipelined commands in order), so
// onUpstreamControlError must POP the per-host FIFO head for it too — not only for a real -ERR.
// Without the pop the FIFO desyncs by one: the channel's genuine SSUBSCRIBE ack head-mismatches and
// its pending subscribe hangs to the ack timeout instead of completing.
TEST_F(SubscriptionRegistryTest, NonErrorControlReplyConsumesFifoHeadSoLaterAckCorrelates) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"},
                      sub); // SSUBSCRIBE ch outstanding: mock_host_ FIFO head = (ssubscribe, ch)

  // A non-Error, non-Push reply (middlebox / protocol desync). The connection stays open and this
  // host re-resolves on backoff, but the stale FIFO head MUST be consumed.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  auto reply = std::make_unique<Common::Redis::RespValue>();
  reply->type(Common::Redis::RespType::SimpleString);
  reply->asString() = "OK";
  registry_.onUpstreamControlError(std::move(reply), mock_host_);

  // The backoff retry re-issues ch (a fresh generation) — now at the FIFO head because the stale
  // entry was popped above — so its real ack correlates and delivers the downstream ``subscribe``
  // ack. A leftover stale head would have made this ack read as a superseded attempt (no write).
  registry_.doResubscribe();
  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$2\r\nch\r\n:1\r\n"), false));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
}

// Blocker regression: onPushMessage carries the source host, and the SUNSUBSCRIBE advisory-ack
// check must use it. A channel that just moved A -> B can have an ack still pending on A while B,
// having lost the slot, sends a GENUINE unsolicited SUNSUBSCRIBE. A host-agnostic (channel-only)
// match would let A's pending ack swallow B's invalidation, stranding the channel on B. With
// host-exact matching, B's signal (source host B, which has no pending ack) is correctly an
// invalidation and B is forgotten.
TEST_F(SubscriptionRegistryTest, UnsolicitedSunsubscribeFromNewHostNotSwallowedByOldHostAck) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(
          Invoke(ssubscribeReturnsHost())); // initial subscribe -> A; the topology reroute
                                            // -> B goes via sendUpstreamSsubscribeToHost (E-8)
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // Topology change moves ch A -> B: SUNSUBSCRIBE A (leaves an expected ack for A) + SSUBSCRIBE B.
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.onClusterTopologyChange();
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // now mapped to B

  // Genuine unsolicited SUNSUBSCRIBE arriving FROM B: B has no expected ack (only A does), so treat
  // it as an invalidation and forget the B mapping.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_2_);
  EXPECT_EQ(0,
            registry_.channelHosts().count("ch")); // forgotten (would be 1 if A's ack swallowed it)
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// Blocker regression: an upstream error to a RE-ISSUED SSUBSCRIBE for an already-active channel
// (resubscribe after a connection loss, or a topology reroute) has no pending downstream bucket, so
// the first-cut code silently no-op'd and left the channel stranded on the failed host. It must
// instead drop the stale mapping and schedule a retry so the channel re-resolves.
TEST_F(SubscriptionRegistryTest, ResubscribeSsubscribeErrorReResolvesActiveChannel) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1),
                          mock_host_); // active; pending bucket drained

  // Connection lost -> doResubscribe re-issues SSUBSCRIBE ch (recording it in the host's control
  // FIFO again). A resubscribe creates NO fresh pending_subscribe_acks_ bucket.
  registry_.onUpstreamConnectionClose(mock_host_);
  registry_.doResubscribe();
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // The re-issued SSUBSCRIBE is rejected. With no pending bucket, the registry must drop the stale
  // mapping and schedule a retry rather than silently stranding the channel on the failed host.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "NOPERM no permissions";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
  EXPECT_EQ(
      0, registry_.channelHosts().count("ch")); // stale mapping forgotten (would be 1 -> stranded)
  EXPECT_EQ(1, registry_.subscriptionCount());  // still subscribed, just re-resolving
}

// #1 (a): a re-issued SSUBSCRIBE that errors while the channel has BOTH an existing active
// subscriber AND a fresh joiner that dedup-parked during the resubscribe window must fail the
// joiner AND STILL re-resolve the live channel immediately (scheduleResubscribe) — not return early
// after the joiner rollback, leaving the active subscription stalled until the generation timeout.
TEST_F(SubscriptionRegistryTest, ReissuedSsubscribeErrorWithJoinerStillReResolvesActiveChannel) {
  auto active_sub = makeSubscriber();
  auto joiner_sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, active_sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1),
                          mock_host_); // active_sub live

  // Connection lost -> ch enters the resubscribe window; doResubscribe re-issues it.
  registry_.onUpstreamConnectionClose(mock_host_);
  registry_.doResubscribe();

  // A NEW subscriber joins during the window -> dedup parks it (deferred ack).
  const auto join_result = registry_.subscribe({"ch"}, joiner_sub);
  EXPECT_TRUE(join_result.ack_deferred);

  // The re-issued SSUBSCRIBE errors: the joiner is failed + rolled back, AND the live channel is
  // re-resolved (forgetChannelHost + scheduleResubscribe) rather than left until the generation
  // timeout (pre-fix returned early here).
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "NOPERM no permissions";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
  EXPECT_EQ(0, registry_.channelHosts().count("ch")); // stale mapping forgotten (pre-fix: 1)
  EXPECT_EQ(1, registry_.subscriptionCount());        // active_sub still subscribed
}

// #1 (b): a DUPLICATE SUBSCRIBE from a subscriber that already holds the channel STILL parks behind
// an in-flight re-subscribe (so it is never acked prematurely — F1/G5), but its pending entry is
// tagged newly_added=false: a failed re-subscribe must NOT roll back (and close) the live
// subscription the duplicate merely echoes. On that failure the subscriber stays subscribed AND —
// since real Redis acks a duplicate SUBSCRIBE unconditionally — receives an immediate echo-ack.
TEST_F(SubscriptionRegistryTest, DuplicateSubscribeDuringResubscribeWindowSurvivesReissueError) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // sub live

  registry_.onUpstreamConnectionClose(mock_host_);
  registry_.doResubscribe(); // ch in the resubscribe window

  // sub sends a DUPLICATE SUBSCRIBE ch: it still PARKS (so it is never acked prematurely — F1/G5),
  // but its pending entry is tagged newly_added=false because sub already held the channel.
  const auto dup_result = registry_.subscribe({"ch"}, sub);
  EXPECT_TRUE(dup_result.ack_deferred);

  // The re-issued SSUBSCRIBE errors: failPendingSubscribers does NOT roll back the
  // newly_added=false duplicate (sub keeps its live subscription while the channel re-resolves —
  // pre-fix the duplicate rolled sub back to 0 and closed it). Since sub's subscription survives,
  // the duplicate's echo-ack
  // (``subscribe ch 1``) is delivered now rather than dropped unanswered.
  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$2\r\nch\r\n:1\r\n"), false));
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "NOPERM no permissions";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
  EXPECT_EQ(1, registry_.subscriptionCount()); // sub STILL subscribed
}

// Blocker regression: an unsolicited SUNSUBSCRIBE only invalidates the mapping if it comes from the
// channel's CURRENT owner. After ch re-resolves A -> B, a late/duplicate SUNSUBSCRIBE still
// arriving from the OLD owner A (Redis can emit both an invalidation and our own ack around a
// migration) must NOT erase B's live mapping.
TEST_F(SubscriptionRegistryTest, LateSunsubscribeFromOldOwnerDoesNotForgetNewHost) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // initial -> A; topology reroute -> B goes via
                                                  // sendUpstreamSsubscribeToHost (E-8)
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.onClusterTopologyChange(); // SUNSUBSCRIBE A (expected ack) + SSUBSCRIBE B (mapping B)
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // First SUNSUBSCRIBE from A is our advisory ack — consumed, B mapping kept.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));
  // A SECOND, late SUNSUBSCRIBE from A (no longer the owner) must NOT erase B's mapping.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // B kept (would be 0 without the owner check)
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// A1-1: a re-issued SSUBSCRIBE for an already-active channel that errors (no pending bucket) on the
// host's LAST channel must RETIRE the now-idle dedicated subscription connection. Every other
// last-channel path closes it, so skipping the retire here leaks an idle connection + Redis-side
// subscription state. (ResubscribeSsubscribeErrorReResolvesActiveChannel covers the re-resolve;
// this pins the retire the earlier test left unasserted.)
TEST_F(SubscriptionRegistryTest, ReissuedSsubscribeErrorRetiresIdleLastChannelConnection) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1),
                          mock_host_); // active on host

  registry_.onUpstreamConnectionClose(mock_host_);
  registry_.doResubscribe(); // re-issues SSUBSCRIBE ch, recording a fresh control-FIFO entry

  // ch is the host's ONLY channel; when its re-issued SSUBSCRIBE errors, the connection is now idle
  // and must be retired (A1-1) in addition to re-resolving the channel.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  EXPECT_CALL(upstream_callbacks_, retireSubscriptionConnectionIfIdle(_));
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "NOPERM no permissions";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
}

// A1-2: after ch re-resolves A -> B, a late/duplicate unsolicited SUNSUBSCRIBE STILL arriving from
// the OLD owner A (past its consumed advisory ack) proves A no longer owns ch. If ch was A's last
// channel, A's dedicated subscription connection is now idle and must be retired — the stale-ignore
// branch previously returned without retiring, leaking A's zombie connection.
TEST_F(SubscriptionRegistryTest, StaleUnsolicitedSunsubscribeRetiresIdleSourceHost) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // initial -> A
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // Reroute ch A -> B: SUNSUBSCRIBE A (expected ack) + SSUBSCRIBE B (via the dry-resolved host,
  // E-8).
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.onClusterTopologyChange(); // ch -> B; A now owns no channel
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // A's first SUNSUBSCRIBE is our advisory ack — consumed, no retire.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  // A's SECOND, late SUNSUBSCRIBE (A is no longer the owner) hits the stale-ignore branch, which
  // must retire A's now-idle connection (A1-2) while leaving B's mapping intact.
  EXPECT_CALL(upstream_callbacks_, retireSubscriptionConnectionIfIdle(_));
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // B still owns ch
}

// S6-2: a single send failure during doResubscribe (a rate-limited / no-host channel) must
// reschedule ONLY via scheduleResubscribe — the failed channel stays in the pending set — and must
// NOT fall back to the whole-registry owner-less nuke (the old onUpstreamConnectionClose ->
// forgetHostChannels(null) that cleared EVERY healthy channel's owner and re-SSUBSCRIBE'd them each
// backoff, re-opening the S6-1/S6-3 windows). The channel that re-subscribed fine must keep its
// owner.
TEST_F(SubscriptionRegistryTest, PartialResubscribeSendFailureDoesNotNukeSucceededChannelOwner) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("keep", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("fail", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())) // initial subscribe OK
      .WillRepeatedly(Return(nullptr));          // every re-subscribe send fails
  subscribeChannels(registry_, {"keep", "fail"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "keep", 1), mock_host_);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "fail", 2), mock_host_);

  // Connection lost: both channels are marked for re-subscribe with their owner KEPT (D1).
  registry_.onUpstreamConnectionClose(mock_host_);
  // doResubscribe re-issues both; "fail"'s send fails -> scheduleResubscribe (retry the failed
  // one), NOT a whole-registry nuke.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_)).Times(testing::AtLeast(1));
  registry_.doResubscribe();

  // "keep" retains its owner. The pre-fix nuke would have cleared it to 0 and re-stormed it.
  EXPECT_EQ(1, registry_.channelHosts().count("keep"));
  EXPECT_EQ(2, registry_.subscriptionCount()); // both still subscribed
}

// S6-4: an unsolicited SUNSUBSCRIBE that invalidates ONE channel (x) of a host must never tear down
// the connection still carrying that host's OTHER channels (y, z). The dangerous window only opens
// if y/z are OWNER-LESS — hostHasSubscriptions then reads false and the retire gate wrongly nukes
// the connection carrying them. Under D1 a kept-connection control-error KEEPS their owner, so
// hostHasSubscriptions stays true and the gate never fires. The DISCRIMINATOR (a check that fails
// on the pre-fix owner-less-ing) is the hostHasSubscriptions assertion right after the
// control-error; asserting only the post-invalidation state would pass on pre-fix code too (with
// y/z owned, hostHasSubscriptions is trivially true), so this drives the host through the
// keep-owner path first.
TEST_F(SubscriptionRegistryTest,
       KeptConnectionControlErrorKeepsOwnerSoInvalidationDoesNotNukeOthers) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost())); // x, y, z all land on host A
  subscribeChannels(registry_, {"x", "y", "z"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "x", 1), mock_host_);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "y", 2), mock_host_);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "z", 3), mock_host_);

  // A kept-connection control-error (a non-Error, non-Push reply mid-reshard — middlebox/desync
  // noise): pre-D1 this owner-less-ed the host's channels; post-D1 it KEEPS their owner and just
  // re-resolves the host on backoff (scheduleResubscribeForHost).
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_)).Times(testing::AtLeast(1));
  auto non_error = std::make_unique<Common::Redis::RespValue>();
  non_error->type(Common::Redis::RespType::SimpleString);
  non_error->asString() = "OK";
  registry_.onUpstreamControlError(std::move(non_error), mock_host_);
  // DISCRIMINATOR: the connection carrying x/y/z is still OWNED (pre-fix this was false —
  // owner-less — and the next invalidation's retire gate would then nuke the connection carrying
  // y/z).
  EXPECT_TRUE(registry_.hostHasSubscriptions(mock_host_));

  // An unsolicited SUNSUBSCRIBE now invalidates only x. Because the host still owns y/z, the retire
  // gate is a no-op and y/z keep their mapping.
  EXPECT_CALL(upstream_callbacks_, requestTopologyRefresh());
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "x", 0), mock_host_);
  EXPECT_EQ(0, registry_.channelHosts().count("x")); // x forgotten (re-resolving)
  EXPECT_EQ(1, registry_.channelHosts().count("y")); // y kept (would be 0 if the connection nuked)
  EXPECT_EQ(1, registry_.channelHosts().count("z")); // z kept
  EXPECT_TRUE(registry_.hostHasSubscriptions(mock_host_));
  EXPECT_EQ(3, registry_.subscriptionCount()); // all three still subscribed (x re-resolving)
}

// A1-3: forgetHostConnectionLedger must clear a host's outstanding control-FIFO even when the
// registry is momentarily EMPTY — the exact state the conn_pool now calls it in on an involuntary
// close (the pre-fix ``!empty()`` gate skipped it). A surviving stale FIFO head head-mismatches —
// and thus BLACKS OUT — the host's NEXT subscribe epoch: the new channel's ssubscribe ack is not
// consumed (consumeAck needs an exact head match), ack_is_current is false, and its downstream
// subscribe never delivers. A real single-threaded conn_pool onEvent can't reach this state (every
// path that empties the registry retires + clears the ledger first), so this drives the registry
// method the fix calls unconditionally, then proves the next epoch is unblocked.
TEST_F(SubscriptionRegistryTest,
       ForgetHostConnectionLedgerOnEmptyRegistryUnblocksNextSubscribeEpoch) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  // Leave a STALE sunsubscribe head on the host: subscribe+ack "stale", then unsubscribe it with
  // the upstream reporting AckExpected (connection kept — NOT ConnectionRetired), so the registry
  // records an outstanding (sunsubscribe, stale) it never clears. subscriptions_ is now empty.
  registry_.subscribe({"stale"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "stale", 1), mock_host_);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("stale", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.unsubscribe({"stale"}, sub);
  ASSERT_TRUE(registry_.empty()); // registry momentarily empty, yet the stale FIFO head survives

  // The conn_pool's involuntary-close hook clears the host's ledger UNCONDITIONALLY (A1-3), even
  // with an empty registry.
  registry_.forgetHostConnectionLedger(mock_host_);

  // The host's NEXT subscribe epoch must now complete: with the stale head gone, "keep"'s
  // ssubscribe ack is consumed (ack_is_current == true) and the downstream subscribe ack is
  // delivered. Without the clear the stale head would head-mismatch this ack and this write would
  // never happen.
  registry_.subscribe({"keep"}, sub);
  EXPECT_CALL(*connections_[0], write(_, false)); // the delivered "subscribe keep" ack
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "keep", 1), mock_host_);
}

// Blocker regression: a SUNSUBSCRIBE that errors upstream will never ack, so its expected-ack
// bookkeeping must be dropped on the error path. Otherwise a later GENUINE unsolicited SUNSUBSCRIBE
// for the same host/channel would be swallowed as this stale advisory expectation.
TEST_F(SubscriptionRegistryTest, SunsubscribeErrorDropsExpectedAckSoLaterUnsolicitedInvalidates) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost())); // stays on A in this single-host fixture
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // Reroute ch A -> B: SUNSUBSCRIBE the OLD owner A (recording an expected ack for A) and
  // SSUBSCRIBE the new owner B. With E-8 the reissue sends to the dry-resolved host, so ch
  // genuinely maps to B (the old mock trick of reporting A from the send no longer applies).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillRepeatedly(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_))  // A -> B
      .WillOnce(Return(mock_host_));   // B -> A
  registry_.onClusterTopologyChange(); // ch -> B; expected SUNSUBSCRIBE ack outstanding for A

  // A's SUNSUBSCRIBE errors upstream: it will never ack, so A's expected-ack bookkeeping must be
  // dropped (correlated via A's control-FIFO head).
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "ERR something went wrong";
  registry_.onUpstreamControlError(std::move(err), mock_host_);

  // Reroute ch back B -> A so A is the CURRENT owner again. A carries NO expected SUNSUBSCRIBE ack
  // — it was dropped on the error above, not left dangling.
  registry_.onClusterTopologyChange(); // ch -> A
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // A genuine unsolicited SUNSUBSCRIBE from the current owner A must INVALIDATE — not be swallowed
  // as the stale expected ack the error already dropped (which would wrongly leave count == 1).
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(0,
            registry_.channelHosts().count("ch")); // forgotten (would be 1 if stale ack swallowed)
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// Issue 1 (round-4): an unsolicited SUNSUBSCRIBE from the current owner (it lost the slot) must not
// just forget the mapping and wait for an unrelated topology event — it must proactively request a
// topology refresh AND schedule a backoff re-resolve so the channel re-subscribes to its new owner
// promptly instead of sitting subscribed-but-not-upstream-subscribed.
TEST_F(SubscriptionRegistryTest, UnsolicitedSunsubscribeRequestsRefreshAndReResolves) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  EXPECT_CALL(upstream_callbacks_, requestTopologyRefresh());
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_); // A lost slot
  EXPECT_EQ(0, registry_.channelHosts().count("ch")); // forgotten, queued for re-resolution
  EXPECT_EQ(1, registry_.subscriptionCount());
}

// Issue 2 (round-4): pending subscribe acks are host-correlated. After ch moves A -> B (downstream
// subscribe still pending), a LATE ssubscribe ack from the OLD owner A must NOT hand the client a
// premature ``subscribe`` success — only the CURRENT owner B's ack completes it.
TEST_F(SubscriptionRegistryTest, LateOldHostSsubscribeAckDoesNotPrematurelySucceedSubscribe) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // initial -> A (downstream ack pending); topology
                                                  // reroute -> B via sendUpstreamSsubscribeToHost
                                                  // (E-8)
  registry_.subscribe({"ch"}, sub);
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.onClusterTopologyChange(); // ch now mapped to B; downstream subscribe still pending

  // Late ack from the OLD owner A: no downstream subscribe ack is written.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
  testing::Mock::VerifyAndClearExpectations(connections_[0]);

  // The CURRENT owner B's ack completes the subscribe (downstream ack written).
  EXPECT_CALL(*connections_[0], write(_, _));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_2_);
}

// Issue 2 (round-5): the OWNER-LESS gap. If an unsolicited SUNSUBSCRIBE (slot moved off the owner)
// forgets the mapping while a fresh subscribe is still pending, a late ack from the DEPARTED owner
// must be rejected — accepting it (there is no owner to compare against) would hand the client a
// ``subscribe`` success with no live upstream subscription until re-resolution.
TEST_F(SubscriptionRegistryTest, OwnerlessGapSsubscribeAckDoesNotSucceedSubscribe) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost())); // owner = A (mock_host_)
  registry_.subscribe({"ch"}, sub);                     // pending on A, no upstream ack yet

  // Unsolicited SUNSUBSCRIBE from A forgets the mapping (owner-less gap) and queues a re-resolve.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_EQ(0, registry_.channelHosts().count("ch")); // owner-less

  // A's late ssubscribe ack must be REJECTED: no premature downstream subscribe success.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
}

// Issue 3 (round-5): a re-subscribe generation's ack is host-correlated. A stale ack from a
// SUPERSEDED host must NOT clear the channel and disarm the generation timer while the current
// attempt is still unacked. Needs a dispatcher for the timer.
TEST_F(SubscriptionRegistryTest, StaleHostSsubscribeAckDoesNotDisarmGenerationTimer) {
  NiceMock<Event::MockDispatcher> dispatcher;
  // Reverse creation order: createTimer #1 (subscribe-ack) -> the later-created timer, #2
  // (generation) -> gen_timer (gmock matches createTimer newest-first).
  auto* gen_timer = new NiceMock<Event::MockTimer>(&dispatcher);
  new NiceMock<Event::MockTimer>(&dispatcher); // subscribe-ack timer (createTimer #1)
  SubscriptionRegistry registry(upstream_callbacks_, random_, dispatcher);

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()))   // initial -> A
      .WillOnce(Invoke(ssubscribeReturnsHost2())); // resubscribe -> B
  registry.subscribe({"ch"}, sub);                 // createTimer #1
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // A's connection drops -> doResubscribe re-resolves ch to B and arms the generation timeout.
  registry.onUpstreamConnectionClose(mock_host_);
  EXPECT_CALL(*gen_timer, enableTimer(std::chrono::milliseconds(10000), _));
  registry.doResubscribe(); // ch now expected on B; pending_resubscribe_channels_ = {ch} //
                            // createTimer #2

  // A STALE ack from the OLD owner A must NOT clear ch or disarm the timer.
  EXPECT_CALL(*gen_timer, disableTimer()).Times(0);
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
  testing::Mock::VerifyAndClearExpectations(gen_timer);

  // The CURRENT owner B's ack clears the last channel and disarms the timer.
  EXPECT_CALL(*gen_timer, disableTimer());
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_2_);
}

// Issue 3 (round-5, SAME-HOST stale generation) — the case host-only correlation could NOT catch,
// and the reason each SSUBSCRIBE attempt carries a generation. ch is re-issued A -> B -> A across
// two connection losses; A's ORIGINAL (gen1) SSUBSCRIBE ack then arrives while ch is mapped back to
// A (gen3). Its host equals the current owner, but its generation is stale, so it must be rejected
// — only the gen3 ack completes the still-pending downstream subscribe. (Connection-loss reissues
// send no SUNSUBSCRIBE, so A's control FIFO is a clean [gen1, gen3] and the acks correlate by
// generation.)
TEST_F(SubscriptionRegistryTest, SameHostStaleGenerationAckRejected) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // subscribe -> A (gen1); the two topology
                                                  // reroutes (-> B gen2, -> A gen3) now send via
                                                  // sendUpstreamSsubscribeToHost with the
                                                  // dry-resolved host (E-8 fixture default)
  registry_.subscribe({"ch"}, sub);               // pending on A (gen1), no ack yet
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillRepeatedly(Return(SunsubscribeResult::AckExpected));

  // Reroute A -> B, then B -> A via topology change. The connection to A stays OPEN across the
  // moves (unlike a connection loss, which clears the host's control FIFO), so A's control FIFO
  // keeps ch's ORIGINAL (gen1) SSUBSCRIBE ahead of the A->B SUNSUBSCRIBE and the new (gen3)
  // SSUBSCRIBE.
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_))  // A -> B
      .WillOnce(Return(mock_host_));   // B -> A
  registry_.onClusterTopologyChange(); // ch -> B (gen2)
  registry_.onClusterTopologyChange(); // ch -> A (gen3), same host, new generation

  // A acks in FIFO (send) order. (1) the ORIGINAL gen1 SSUBSCRIBE: its host equals the current
  // owner A, but its generation is stale — so NO downstream subscribe success. This is the exact
  // ack a host-only check would wrongly accept.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
  // (2) the advisory ack to the A->B SUNSUBSCRIBE we sent A — drains the FIFO down to the gen3
  // head.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  testing::Mock::VerifyAndClearExpectations(connections_[0]);

  // (3) the CURRENT gen3 SSUBSCRIBE ack completes the still-pending downstream subscribe.
  EXPECT_CALL(*connections_[0], write(_, _));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
}

// Round-5 re-review blocker: the SSUBSCRIBE ERROR path must apply the same current-attempt gate as
// the ack path. A stale same-host error (A -> B -> A: gen1 error arriving while ch awaits gen3 on
// A) must NOT fail/roll back the pending downstream subscribe — the later gen3 ack still completes
// it.
TEST_F(SubscriptionRegistryTest, StaleSsubscribeErrorDoesNotFailPendingSubscribe) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // subscribe -> A (gen1); the two topology
                                                  // reroutes (-> B gen2, -> A gen3) now send via
                                                  // sendUpstreamSsubscribeToHost with the
                                                  // dry-resolved host (E-8 fixture default)
  registry_.subscribe({"ch"}, sub);               // pending on A (gen1)
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillRepeatedly(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_))  // A -> B
      .WillOnce(Return(mock_host_));   // B -> A
  registry_.onClusterTopologyChange(); // ch -> B (gen2)
  registry_.onClusterTopologyChange(); // ch -> A (gen3); A's FIFO head is still the gen1 SSUBSCRIBE

  // A's stale (gen1) SSUBSCRIBE error correlates to the gen1 FIFO head but its generation is stale:
  // it must be ignored entirely — neither closing the subscriber's connection (the F3 fail action)
  // nor writing anything downstream, and not rolling the subscription back.
  EXPECT_CALL(*connections_[0], close(_)).Times(0);
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "NOPERM no permissions";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
  EXPECT_EQ(1, registry_.subscriptionCount());        // NOT rolled back
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // current mapping intact
  testing::Mock::VerifyAndClearExpectations(connections_[0]);

  // Drain the A->B SUNSUBSCRIBE advisory ack, then A's CURRENT gen3 ack completes the subscribe.
  registry_.onPushMessage(makeSubscribeAckPush("sunsubscribe", "ch", 0), mock_host_);
  EXPECT_CALL(*connections_[0], write(_, _)); // subscribe success
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
}

// Round-5 re-review blocker: a stale SSUBSCRIBE error for an ACTIVE channel must not forget the
// current owner mapping. ch is active on A, rerouted A -> B -> A; B's superseded (gen2) error then
// arrives and must NOT erase ch's current {A, gen3} mapping (which would strand the live channel).
TEST_F(SubscriptionRegistryTest, StaleSsubscribeErrorDoesNotForgetCurrentMapping) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // subscribe -> A (gen1); the two topology
                                                  // reroutes (-> B gen2, -> A gen3) now send via
                                                  // sendUpstreamSsubscribeToHost with the
                                                  // dry-resolved host (E-8 fixture default)
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillRepeatedly(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_))  // A -> B (gen2)
      .WillOnce(Return(mock_host_));   // B -> A (gen3)
  registry_.onClusterTopologyChange(); // ch -> B (gen2), B's FIFO head is the gen2 SSUBSCRIBE
  registry_.onClusterTopologyChange(); // ch -> A (gen3)

  // B's stale (gen2) SSUBSCRIBE error: host B is no longer the owner (A is, gen3), so it must be
  // ignored — the current mapping stays rather than being forgotten and stranded.
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "NOPERM no permissions";
  registry_.onUpstreamControlError(std::move(err), mock_host_2_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // preserved (would be 0 if wrongly forgotten)
}

// Issue 4 (round-5): the topology-reroute path arms the generation timeout too, not only
// doResubscribe — so a silently-lost SSUBSCRIBE ack on a rerouted channel is retried on backoff
// rather than stranding it. Needs a dispatcher for the timer.
TEST_F(SubscriptionRegistryTest, TopologyRerouteArmsGenerationTimeout) {
  NiceMock<Event::MockDispatcher> dispatcher;
  auto* gen_timer = new NiceMock<Event::MockTimer>(&dispatcher);
  new NiceMock<Event::MockTimer>(&dispatcher); // subscribe-ack timer (createTimer #1)
  SubscriptionRegistry registry(upstream_callbacks_, random_, dispatcher);

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // initial -> A; topology reroute -> B via
                                                  // sendUpstreamSsubscribeToHost (E-8)
  registry.subscribe({"ch"}, sub);                // createTimer #1
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // Owner moves A -> B: onClusterTopologyChange reroutes and arms the generation timeout.
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(*gen_timer, enableTimer(std::chrono::milliseconds(10000), _));
  registry.onClusterTopologyChange(); // createTimer #2 (generation)

  // ch never re-acks on B; firing the timeout schedules a backoff retry.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  gen_timer->invokeCallback();
}

// Finding 3 (under D1's keep-owner model): a resubscribe-generation timeout must re-resolve ONLY
// the still-unacked channels of the generation, not every subscribed channel. The retry scope IS
// pending_resubscribe_channels_ — a channel leaves it when its current-attempt ack lands — so the
// timeout re-issues exactly the leftover unacked channel and the acked sibling is untouched.
// Neither owner is cleared (D1): reissueSsubscribe refreshes the owner when doResubscribe re-sends.
TEST_F(SubscriptionRegistryTest, GenerationTimeoutReResolvesOnlyUnackedChannels) {
  NiceMock<Event::MockDispatcher> dispatcher;
  auto* gen_timer = new NiceMock<Event::MockTimer>(&dispatcher);
  new NiceMock<Event::MockTimer>(&dispatcher); // subscribe-ack timer (createTimer #1)
  SubscriptionRegistry registry(upstream_callbacks_, random_, dispatcher);

  // Capture re-issue order so the acks below match the per-host FIFO head order (see
  // BackoffResetsOnlyWhenResubscribeGenerationFullyAcks); the reissue order follows subscriptions_
  // hash order, not "a" then "b".
  std::vector<std::string> resent;
  ON_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillByDefault(
          Invoke([&](const std::string& ch,
                     Common::Redis::Client::PushMessageCallbacks&) -> Upstream::HostConstSharedPtr {
            resent.push_back(ch);
            return mock_host_;
          }));

  auto sub = makeSubscriber();
  subscribeChannels(registry, {"a", "b"}, sub); // both on mock_host_
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 1), mock_host_);
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "b", 2), mock_host_);

  // Connection loss re-issues the generation {a, b} and arms the generation timer.
  registry.onUpstreamConnectionClose(mock_host_);
  resent.clear();
  registry.doResubscribe();
  ASSERT_EQ(2, resent.size());
  const std::string acked = resent[0];
  const std::string unacked = resent[1];

  // Only ONE channel of the generation re-acks; the other stays in pending_resubscribe_channels_.
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", acked, 1), mock_host_);
  EXPECT_EQ(1, registry.channelHosts().count(acked));
  EXPECT_EQ(1, registry.channelHosts().count(unacked)); // both mapped after the re-issue

  // The generation timeout fires: under keep-owner it re-resolves ONLY the still-unacked channel
  // (the acked sibling already left pending_resubscribe_channels_), WITHOUT clearing either owner —
  // reissueSsubscribe refreshes the owner when doResubscribe re-sends. Both mappings stay, and the
  // next doResubscribe re-issues just the unacked channel.
  resent.clear();
  gen_timer->invokeCallback();                        // handleResubscribeGenerationTimeout
  EXPECT_EQ(1, registry.channelHosts().count(acked)); // acked: untouched
  EXPECT_EQ(
      1, registry.channelHosts().count(unacked)); // unacked: owner KEPT (D1), re-resolved on send
  registry.doResubscribe();
  EXPECT_THAT(resent, testing::ElementsAre(unacked)); // ONLY the unacked channel re-issued
}

// S6-1 (D1 keep-owner): a re-subscribe signal on a KEPT connection seeds the channel for
// re-subscribe but leaves its owner intact through the backoff window, so a last-subscriber
// UNSUBSCRIBE arriving in that window still finds the owner and sends the upstream SUNSUBSCRIBE —
// the subscription is not silently leaked on the live connection (the former owner-less-at-signal
// model dropped the SUNSUBSCRIBE and stranded the upstream subscription).
TEST_F(SubscriptionRegistryTest, KeptConnectionSignalKeepsOwnerSoWindowUnsubscribeSunsubscribes) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active
  EXPECT_EQ(1, registry_.channelHosts().count("ch"));

  // A non-Error control reply keeps the connection open and seeds "ch" for re-subscribe — WITHOUT
  // clearing its owner (D1).
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  auto reply = std::make_unique<Common::Redis::RespValue>();
  reply->type(Common::Redis::RespType::SimpleString);
  reply->asString() = "OK";
  registry_.onUpstreamControlError(std::move(reply), mock_host_);
  EXPECT_EQ(1, registry_.channelHosts().count("ch")); // owner KEPT during the window (D1)

  // In the window the last subscriber unsubscribes: because the owner is still mock_host_, the
  // registry sends the upstream SUNSUBSCRIBE rather than skipping it (S6-1).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.unsubscribe({"ch"}, sub);
  EXPECT_EQ(0, registry_.channelHosts().count("ch")); // forgotten by the unsubscribe itself
}

// S6-3 (D1): a new subscriber that joins a channel while it is in the re-subscribe window (seeded
// in pending_resubscribe_channels_ at signal time, its new-owner ack not yet re-confirmed) is
// DEFERRED and ack-timeout-protected, not handed a premature success ack.
TEST_F(SubscriptionRegistryTest, DedupJoinDuringResubscribeWindowIsDeferred) {
  auto sub1 = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub1);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active

  // Connection loss seeds "ch" into the re-subscribe window (owner kept).
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onUpstreamConnectionClose(mock_host_);

  // A second subscriber joins "ch" DURING the window: it must defer, not get an immediate success —
  // the channel is not upstream-confirmed on its new owner yet.
  auto sub2 = makeSubscriber();
  const auto result = registry_.subscribe({"ch"}, sub2);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.ack_deferred); // S6-3: deferred, not premature success
}

// MISC-2: a duplicate pipelined SUBSCRIBE (``SUBSCRIBE ch ch``) parks two pending entries for the
// SAME subscriber on one channel, but on upstream failure the rollback counts
// pubsub_subscribe_ack_error ONCE (matching the single gauge decrement), not once per entry — else
// the documented identity active = subscribe_total − unsubscribe_total − ack_error drifts by −1.
TEST_F(SubscriptionRegistryTest, DuplicateSubscribeFailureCountsAckErrorOnce) {
  Stats::IsolatedStoreImpl store;
  Stats::Counter& success = store.rootScope()->counterFromString("pubsub_subscribe_ack_success");
  Stats::Counter& error = store.rootScope()->counterFromString("pubsub_subscribe_ack_error");
  auto sub = makeSubscriberWithAckCounters(success, error);

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("dup", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())); // one upstream SSUBSCRIBE for the first arg
  // ``SUBSCRIBE dup dup``: the second call dedups onto the same still-pending bucket (two entries).
  registry_.subscribe({"dup"}, sub);
  registry_.subscribe({"dup"}, sub);

  // The upstream rejects the SSUBSCRIBE: both pending entries fail, but the error is counted once
  // (per subscriber+channel) and the gauge rolls back once.
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "ERR no such command";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
  EXPECT_EQ(1U, error.value());   // MISC-2: once, not twice
  EXPECT_EQ(0U, success.value()); // never confirmed
}

// Issue 6 (round-4): ack-time subscribe counters reflect the REAL async outcome — success when the
// upstream ack lands, error when the upstream rejects it — unlike the send-time command.subscribe
// stat.
TEST_F(SubscriptionRegistryTest, SubscribeAckCountersRecordAsyncOutcome) {
  Stats::IsolatedStoreImpl store;
  Stats::Counter& success = store.rootScope()->counterFromString("pubsub_subscribe_ack_success");
  Stats::Counter& error = store.rootScope()->counterFromString("pubsub_subscribe_ack_error");

  auto sub = makeSubscriberWithAckCounters(success, error);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ok"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ok", 1), mock_host_); // upstream acks
  EXPECT_EQ(1U, success.value());
  EXPECT_EQ(0U, error.value());

  // A second channel whose re-issued SSUBSCRIBE the upstream rejects records an error, not a
  // success.
  registry_.subscribe({"bad"}, sub);
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "NOPERM no permissions";
  registry_.onUpstreamControlError(std::move(err), mock_host_); // FIFO head = ssubscribe "bad"
  EXPECT_EQ(1U, success.value());
  EXPECT_EQ(1U, error.value());
}

// --- Unknown push message type ---

TEST_F(SubscriptionRegistryTest, UnknownPushTypeIgnored) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  auto value = std::make_unique<Common::Redis::RespValue>();
  value->type(Common::Redis::RespType::Push);
  std::vector<Common::Redis::RespValue> array(2);
  array[0].type(Common::Redis::RespType::BulkString);
  array[0].asString() = "invented_type";
  array[1].type(Common::Redis::RespType::BulkString);
  array[1].asString() = "data";
  value->asArray().swap(array);

  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  EXPECT_NO_THROW(registry_.onPushMessage(std::move(value), mock_host_));
}

// --- Subscription ack delivers the subscriber's per-subscriber count ---

// Subscribe-ack from upstream fires the deferred downstream ack with the subscriber's
// per-subscriber count (NOT the upstream count, which is registry-wide). The test below
// pins both behaviors: (a) the ack reaches the subscriber's connection, and (b) the count
// in the delivered ack reflects subscriber state, not upstream's.
TEST_F(SubscriptionRegistryTest, SubscribeAckDeliversToPendingSubscriber) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  // Upstream's count (7) is intentionally different from the subscriber's per-subscriber count (1).
  // The delivered ack should carry the subscriber's count and the literal ``subscribe`` verb.
  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$2\r\nch\r\n:1\r\n"), false));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 7), mock_host_);
}

// Subscribe-ack with no pending entry (extra/late upstream ack, e.g. after rollback or
// resubscribe race) is silently dropped — must not throw, must not write.
TEST_F(SubscriptionRegistryTest, SubscribeAckWithNoPendingDropsSilently) {
  auto sub = makeSubscriber();
  // Note: NOT calling registry_.ssubscribe — no pending entry exists.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  EXPECT_NO_THROW(
      registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "stranger", 1), mock_host_));
}

// Disconnect-before-ack: subscriber goes away before the upstream subscribe-ack arrives.
// The pending entry's weak_ptr expires; drain becomes a no-op (no crash, no delivery).
TEST_F(SubscriptionRegistryTest, SubscribeAckAfterSubscriberDisconnect) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  // Simulate downstream disconnect: removeSubscriber drops the pending entry directly,
  // so even if the weak_ptr were still valid, no entry would be in the pending map.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.removeSubscriber(sub);
  sub.reset();

  EXPECT_NO_THROW(registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_));
}

// F1: a second subscriber to a channel whose first upstream ssubscribe ack has NOT yet arrived must
// not be handed a premature success (the upstream could still reject). It joins the still-open
// pending bucket (ack_deferred), issues no fresh upstream send, and the single upstream ack then
// delivers the downstream ``subscribe`` ack to BOTH subscribers.
TEST_F(SubscriptionRegistryTest, DedupWhileSubscribingDefersAckUntilUpstreamAck) {
  auto sub_a = makeSubscriber(); // connections_[0]
  auto sub_b = makeSubscriber(); // connections_[1]

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  auto res_a = registry_.subscribe({"ch"}, sub_a);
  EXPECT_TRUE(res_a.ack_deferred);

  // Second subscriber BEFORE the upstream ack: dedup on a still-subscribing channel — no fresh
  // upstream send, and its ack is deferred (not fabricated immediately).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _)).Times(0);
  auto res_b = registry_.subscribe({"ch"}, sub_b);
  EXPECT_TRUE(res_b.ack_deferred);

  // The single upstream ssubscribe ack delivers a downstream ``subscribe`` ack to BOTH subscribers,
  // each carrying its own per-subscriber count (1).
  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$2\r\nch\r\n:1\r\n"), false));
  EXPECT_CALL(*connections_[1],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$2\r\nch\r\n:1\r\n"), false));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 7), mock_host_);
}

// F4: a subscribe whose upstream ack never arrives has its connection closed and its subscription
// rolled back when the timeout fires (F3), instead of hanging the SUBSCRIBE forever. The timer is
// armed only when a dispatcher is present (production), so this test constructs a registry with a
// mock dispatcher/timer.
TEST_F(SubscriptionRegistryTest, SubscribeAckTimeoutClosesSubscriberAndRollsBack) {
  NiceMock<Event::MockDispatcher> dispatcher;
  auto* timer = new NiceMock<Event::MockTimer>(&dispatcher); // returned by the next createTimer()
  SubscriptionRegistry registry(upstream_callbacks_, random_, dispatcher);

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(10000), _));
  registry.subscribe({"ch"}, sub);

  // Firing the timeout closes the subscriber's connection (F3, instead of a desyncing out-of-band
  // -ERR) and rolls the channel back (SUNSUBSCRIBE, since this was the last subscriber on it).
  EXPECT_CALL(*connections_[0], close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  timer->invokeCallback();

  EXPECT_EQ(0, registry.subscriptionCount());
}

// Issue 3 (round-4): a re-issued SSUBSCRIBE for an already-active channel has no per-bucket ack
// timer, so doResubscribe arms a generation timeout. If a re-sent channel never acks (silent loss /
// a wedged connection), firing the timeout re-resolves it on backoff instead of stalling forever.
TEST_F(SubscriptionRegistryTest, ResubscribeGenerationTimeoutRetries) {
  NiceMock<Event::MockDispatcher> dispatcher;
  // MockTimer registers createTimer_ newest-first, so create in reverse of use: the fresh-subscribe
  // ack timer is returned by createTimer #1, the generation timer by createTimer #2.
  auto* gen_timer = new NiceMock<Event::MockTimer>(&dispatcher);
  new NiceMock<Event::MockTimer>(&dispatcher); // subscribe-ack timer (createTimer #1)
  SubscriptionRegistry registry(upstream_callbacks_, random_, dispatcher);

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry.subscribe({"ch"}, sub); // createTimer #1 (subscribe-ack timer)
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // Connection lost -> doResubscribe re-sends ch and arms the generation timeout (createTimer #2).
  registry.onUpstreamConnectionClose(mock_host_);
  EXPECT_CALL(*gen_timer, enableTimer(std::chrono::milliseconds(10000), _));
  registry.doResubscribe();

  // ch never re-acks; firing the generation timeout schedules a backoff retry.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  gen_timer->invokeCallback();
}

// #1 regression: when an UNSUBSCRIBE (or downstream disconnect) empties the re-subscribe retry
// scope, the backoff must reset AND the generation timer must disarm — the same cleanup the
// current-attempt ack path does. Before the fix only the ack path cleaned up; the unsubscribe /
// orphan erase site dropped the entry but left the backoff escalated and the generation timer armed
// (a stale no-op fire, and an unrelated later loss starting its FIRST retry at the escalated
// backoff). Both now go through forgetPendingResubscribe; disarming the generation timer is the
// observable proxy for that shared cleanup.
TEST_F(SubscriptionRegistryTest, UnsubscribeEmptyingResubscribeScopeDisarmsGenerationTimer) {
  NiceMock<Event::MockDispatcher> dispatcher;
  // Reverse-order matched (gmock matches the most-recently-set expectation first): construct the
  // generation timer FIRST so it is returned by createTimer #2 (the subscribe-ack timer is #1).
  auto* gen_timer = new NiceMock<Event::MockTimer>(&dispatcher);
  new NiceMock<Event::MockTimer>(&dispatcher); // subscribe-ack scheduler timer (createTimer #1)
  SubscriptionRegistry registry(upstream_callbacks_, random_, dispatcher);

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry.subscribe({"a"}, sub); // createTimer #1 (subscribe-ack scheduler)
  registry.subscribe({"b"}, sub);
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 1), mock_host_);
  registry.onPushMessage(makeSubscribeAckPush("ssubscribe", "b", 2), mock_host_);

  // Connection lost -> doResubscribe re-sends a, b and ARMS the generation timer (createTimer #2).
  registry.onUpstreamConnectionClose(mock_host_);
  EXPECT_CALL(*gen_timer, enableTimer(std::chrono::milliseconds(10000), _));
  registry.doResubscribe();

  // Unsubscribing BOTH re-subscribing channels empties the retry scope; the LAST erase must disarm
  // the generation timer (routed through forgetPendingResubscribe). VerifyAndClearExpectations
  // forces the check to be satisfied DURING the unsubscribe, not deferred to the registry's
  // destructor — so the test fails on the pre-fix code (which never disarmed here).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe(_, _))
      .WillRepeatedly(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(*gen_timer, disableTimer());
  registry.unsubscribe({"a", "b"}, sub);
  ::testing::Mock::VerifyAndClearExpectations(gen_timer);
}

// F1 regression: the subscribe-ack timeout rollback must undo the optimistic
// pubsub_active_subscriptions gauge increment the splitter records at subscribe time — otherwise
// the gauge leaks (the later disconnect can only decrement channels still in subscribedChannels(),
// which this rollback already removed).
TEST_F(SubscriptionRegistryTest, SubscribeAckTimeoutDecrementsActiveSubscriptionGauge) {
  NiceMock<Stats::MockGauge> gauge;
  NiceMock<Event::MockDispatcher> dispatcher;
  auto* timer = new NiceMock<Event::MockTimer>(&dispatcher);
  SubscriptionRegistry registry(upstream_callbacks_, random_, dispatcher);

  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();
  auto* raw = connection.get();
  owned_connections_.push_back(std::move(connection));
  auto sub = std::make_shared<DownstreamSubscriber>(
      *raw,
      DownstreamSubscriberStats{throwaway_push_, gauge, throwaway_ack_ok_, throwaway_ack_err_});

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry.subscribe({"ch"}, sub);

  // Timeout rolls back the one channel -> removeChannel decrements the gauge by exactly 1 (A-2: the
  // gauge moves per set mutation via dec(), not a batched sub()).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(gauge, dec());
  timer->invokeCallback();
}

// Findings 2+3 regression: subscribers waiting on the SAME channel's upstream subscribe share ONE
// timeout timer (not one per subscriber — that armed N timers for a fan-in of N and made each
// timeout do an O(N) bucket scan, i.e. O(N^2)). When the single timer fires, EVERY pending
// subscriber is failed and rolled back in one drain, each getting its own error (symmetric with the
// ack path).
TEST_F(SubscriptionRegistryTest, SubscribeAckTimeoutFailsAllPendingSubscribersWithOneTimer) {
  NiceMock<Stats::MockGauge> gauge;
  NiceMock<Event::MockDispatcher> dispatcher;
  auto* timer = new NiceMock<Event::MockTimer>(&dispatcher);
  SubscriptionRegistry registry(upstream_callbacks_, random_, dispatcher);

  auto make_sub = [&]() {
    auto connection = std::make_unique<NiceMock<Network::MockConnection>>();
    auto* raw = connection.get();
    owned_connections_.push_back(std::move(connection));
    return std::make_shared<DownstreamSubscriber>(
        *raw,
        DownstreamSubscriberStats{throwaway_push_, gauge, throwaway_ack_ok_, throwaway_ack_err_});
  };
  auto sub_a = make_sub();
  auto sub_b = make_sub();

  // The first subscriber issues the upstream SSUBSCRIBE and arms the channel's ONLY timer; the
  // second dedups onto the still-pending bucket and must NOT arm a second timer. The MockTimer
  // ctor already installs EXPECT_CALL(createTimer_).WillOnce, so a second arm (the per-subscriber
  // timer regression this fix removes) would over-saturate that expectation and fail the test.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry.subscribe({"ch"}, sub_a);
  registry.subscribe({"ch"}, sub_b);

  // The single timeout fails BOTH subscribers: two rollbacks (removeChannel decrements the gauge
  // once each — A-2), and SUNSUBSCRIBE fires once, when the last subscriber leaves the channel.
  EXPECT_CALL(gauge, dec()).Times(2);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  timer->invokeCallback();

  EXPECT_EQ(0, registry.subscriptionCount());
}

// --- Backoff escalation ---

TEST_F(SubscriptionRegistryTest, BackoffEscalatesWithoutSuccessfulResubscribe) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  // Each close reschedules with a longer backoff (the strategy doubles 100ms -> 200ms -> 400ms
  // under the 30s cap). Don't drive doResubscribe between them (simulates ongoing failure), so the
  // backoff keeps escalating. Delays are jittered, so this asserts only that a reschedule is armed
  // each time; BackoffResetsOnUpstreamAckNotSend pins the reset semantics numerically.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onUpstreamConnectionClose();

  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onUpstreamConnectionClose();

  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onUpstreamConnectionClose();
}

// P8: the re-subscribe backoff is reset only by a genuine upstream ssubscribe *ack*, not by a
// successful fire-and-forget *send*. A permanently-rejecting upstream (whose SSUBSCRIBE send
// "succeeds" but is async-rejected, closing the connection) must keep escalating instead of
// resetting to the floor on every reconnect. The random generator is pinned so the jittered delay
// is deterministic: ``nextBackOffMs()`` returns ``random() % interval``, and ``10^8 - 1`` mod
// {100,200,400} is ``interval - 1``, so the scheduled delay reveals the current backoff interval.
TEST_F(SubscriptionRegistryTest, BackoffResetsOnUpstreamAckNotSend) {
  ON_CALL(random_, random()).WillByDefault(Return(99999999ULL));
  std::vector<int64_t> delays;
  ON_CALL(upstream_callbacks_, scheduleResubscribe(_))
      .WillByDefault(
          Invoke([&delays](std::chrono::milliseconds d) { delays.push_back(d.count()); }));

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  // Each loss is host-scoped (S-4: the closing host is known), clearing that host's control FIFO so
  // the re-issued SSUBSCRIBE records a fresh generation the ack below correlates against — rather
  // than a stale pre-loss entry that would look like a superseded attempt.
  registry_.onUpstreamConnectionClose(mock_host_); // interval 100 -> delay 99
  // A successful *send* must NOT reset the backoff (send-success != ack-success).
  registry_.doResubscribe();
  registry_.onUpstreamConnectionClose(
      mock_host_);           // interval 200 -> delay 199 (escalated despite send)
  registry_.doResubscribe(); // re-issue on the new connection so the ack is the CURRENT attempt

  // A real upstream ssubscribe ack (for the current attempt) resets the backoff.
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);
  registry_.onUpstreamConnectionClose(mock_host_); // interval back to 100 -> delay 99

  EXPECT_THAT(delays, testing::ElementsAre(99, 199, 99));
}

// F2: after a connection loss re-subscribes several channels, the backoff resets only when the
// WHOLE generation acks. A single channel's ack while another is still pending must NOT reset it —
// otherwise a partial failure (one channel keeps failing) hot-loops at the floor. Pinned random
// makes the jittered delay reveal the interval (interval-1); see BackoffResetsOnUpstreamAckNotSend.
TEST_F(SubscriptionRegistryTest, BackoffResetsOnlyWhenResubscribeGenerationFullyAcks) {
  ON_CALL(random_, random()).WillByDefault(Return(99999999ULL));
  std::vector<int64_t> delays;
  ON_CALL(upstream_callbacks_, scheduleResubscribe(_))
      .WillByDefault(
          Invoke([&delays](std::chrono::milliseconds d) { delays.push_back(d.count()); }));

  // Capture the order doResubscribe re-issues channels: Redis acks a connection's fire-and-forget
  // commands in SEND order and consumeControlCommandAck pops the per-host FIFO head, so the acks
  // below must arrive in re-issue order or they read as stale. doResubscribe walks subscriptions_
  // in hash order (not "a" then "b" by construction), so drive the acks off the captured order.
  std::vector<std::string> resent;
  ON_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillByDefault(
          Invoke([&](const std::string& ch,
                     Common::Redis::Client::PushMessageCallbacks&) -> Upstream::HostConstSharedPtr {
            resent.push_back(ch);
            return mock_host_;
          }));

  auto sub = makeSubscriber();
  subscribeChannels(registry_, {"a", "b"}, sub);

  registry_.onUpstreamConnectionClose(mock_host_); // interval 100 -> delay 99
  resent.clear();
  registry_.doResubscribe(); // resubscribe generation = {a, b}
  ASSERT_EQ(2, resent.size());
  // Only ONE channel of the generation acks — the generation is not complete, so the backoff must
  // NOT reset.
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", resent[0], 1), mock_host_);
  registry_.onUpstreamConnectionClose(
      mock_host_); // interval 200 -> delay 199 (partial ack did not reset)
  // The second loss re-subscribes the WHOLE generation again (the host-scoped close cleared
  // ownership of both channels, so the earlier ack is a superseded attempt); the backoff resets
  // only once BOTH channels of this fresh generation ack, in re-issue (FIFO) order.
  resent.clear();
  registry_.doResubscribe();
  ASSERT_EQ(2, resent.size());
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", resent[0], 1),
                          mock_host_); // incomplete
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", resent[1], 1), mock_host_); // complete
  registry_.onUpstreamConnectionClose(mock_host_); // interval back to 100 -> delay 99

  EXPECT_THAT(delays, testing::ElementsAre(99, 199, 99));
}

// F2 (partial SEND failure): a channel whose re-SSUBSCRIBE send FAILS never acks, so it must keep
// the generation incomplete — a sibling channel's ack must not reset the backoff to the floor while
// the failing channel is still down.
TEST_F(SubscriptionRegistryTest, BackoffDoesNotResetWhenAResubscribeSendFailed) {
  ON_CALL(random_, random()).WillByDefault(Return(99999999ULL));
  std::vector<int64_t> delays;
  ON_CALL(upstream_callbacks_, scheduleResubscribe(_))
      .WillByDefault(
          Invoke([&delays](std::chrono::milliseconds d) { delays.push_back(d.count()); }));

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("a", _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("b", _))
      .WillOnce(Invoke(ssubscribeReturnsHost())) // initial subscribe OK
      .WillRepeatedly(Return(nullptr));          // resubscribe send fails
  subscribeChannels(registry_, {"a", "b"}, sub);

  registry_.onUpstreamConnectionClose(mock_host_); // interval 100 -> delay 99
  registry_
      .doResubscribe(); // a OK (fresh generation on the cleared FIFO), b send FAILS; b keeps the
                        // generation incomplete (F2) and the failed send reschedules -> delay 199
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "a", 1),
                          mock_host_); // a acks, b still down
  registry_.onUpstreamConnectionClose(
      mock_host_); // interval 400 -> delay 399: a's ack did NOT reset (F2)

  EXPECT_THAT(delays, testing::ElementsAre(99, 199, 399));
}

// F3: a single host's subscription-connection loss re-subscribes ONLY that host's channels, not
// every channel on every other still-connected host.
TEST_F(SubscriptionRegistryTest, HostScopedResubscribeOnlyReissuesThatHostsChannels) {
  auto host_a = mock_host_;
  auto host_b = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto sub = makeSubscriber();

  std::vector<std::string> resent;
  ON_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillByDefault(
          Invoke([&](const std::string& ch,
                     Common::Redis::Client::PushMessageCallbacks&) -> Upstream::HostConstSharedPtr {
            resent.push_back(ch);
            return (ch == "ch_a") ? host_a : host_b;
          }));

  registry_.subscribe({"ch_a"}, sub);
  registry_.subscribe({"ch_b"}, sub);
  resent.clear(); // ignore the initial sends

  // host_a's subscription connection drops -> doResubscribe re-issues ONLY ch_a (ch_b stays on its
  // still-connected host).
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onUpstreamConnectionClose(host_a);
  registry_.doResubscribe();

  EXPECT_THAT(resent, testing::ElementsAre("ch_a"));
}

// --- RemoveSubscriber cleans up per-shard subscriptions ---

TEST_F(SubscriptionRegistryTest, RemoveSubscriberCleansUpSharded) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("s1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("s2", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));

  registry_.subscribe({"s1"}, sub);
  registry_.subscribe({"s2"}, sub);

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("s1", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("s2", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.removeSubscriber(sub);

  EXPECT_EQ(0, registry_.subscriptionCount());
}

// --- dropHost forgets removed-host mappings so no SUNSUBSCRIBE targets a dead endpoint (C-4) ---

TEST_F(SubscriptionRegistryTest, DropHostForgetsShardedMappingAndSkipsSunsubscribe) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("s1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"s1"}, sub);
  EXPECT_EQ(1, registry_.channelHosts().count("s1"));

  // The shard-owning host is removed from the cluster: dropHost forgets its channel mapping.
  registry_.dropHost(mock_host_);
  EXPECT_EQ(0, registry_.channelHosts().count("s1"));

  // A later unsubscribe for that channel must NOT emit a SUNSUBSCRIBE — there is no live host to
  // send it to, and doing so would auto-open a connection to the decommissioned endpoint. The
  // local subscription state is still cleaned up.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe(_, _)).Times(0);
  registry_.unsubscribe({"s1"}, sub);
  EXPECT_EQ(0, registry_.subscriptionCount());
}

// --- Fan-out that re-enters removeSubscriber must not invalidate the set (B-4) ---

TEST_F(SubscriptionRegistryTest, MessageDeliverReentrantRemoveSubscriberDoesNotCrash) {
  auto sub1 = makeSubscriber();
  auto sub2 = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub1);
  registry_.subscribe({"ch"}, sub2); // dedup: joins the existing channel, no upstream send.

  // sub1's downstream write synchronously removes BOTH subscribers — this models a disconnect
  // that ProxyFilter::onEvent turns into removeSubscriber() in the middle of the fan-out loop.
  // Without the pre-deliver snapshot, removeSubscriber mutates/rehashes the subscriptions_ set
  // being iterated -> iterator use-after-free (caught by ASAN).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  ON_CALL(*connections_[0], write(_, _)).WillByDefault(Invoke([&](Buffer::Instance&, bool) {
    registry_.removeSubscriber(sub1);
    registry_.removeSubscriber(sub2);
  }));

  // Must complete without a crash; both subscribers end up removed.
  registry_.onPushMessage(makeSmessagePush("ch", "hi"), mock_host_);
  EXPECT_EQ(0, registry_.subscriptionCount());
}

// --- Upstream subscribe failure rolls back local state ---

TEST_F(SubscriptionRegistryTest, SsubscribeFailureRollsBack) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("sch", _)).WillOnce(Return(nullptr));

  uint64_t count = registry_.subscribe({"sch"}, sub).subscription_count;
  EXPECT_EQ(0, count);
  EXPECT_EQ(0, registry_.subscriptionCount());
}

// --- clear() empties registry-side state and closes subscribers ---
//
// clear() empties the registry-side maps but intentionally does NOT touch each subscriber's own
// ``subscribed_channels_`` set: under the A-2 gauge model that set is what ~DownstreamSubscriber
// drains the pubsub_active_subscriptions_ gauge by at destruction, so it must survive clear() (see
// ClearClosesAffectedDownstreamConnections). In this registry-only unit test there is no
// ProxyFilter and no destructor driven here, so the subscriber's set simply survives — the expected
// behavior.
TEST_F(SubscriptionRegistryTest, ClearEmptiesRegistryAndClosesSubscriber) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch1"}, sub);
  EXPECT_FALSE(sub->subscribedChannels().empty());

  EXPECT_CALL(*connections_[0], close(Network::ConnectionCloseType::NoFlush));
  registry_.clear();
  EXPECT_TRUE(registry_.empty());
}

// clear() must close every distinct downstream subscriber's connection so a client whose
// cluster has been removed/updated does not silently believe its subscriptions are still live.
// Pins the cluster-removal teardown contract: after clear() the downstream is gone, mirroring
// real Redis dropping subscribers when topology changes invalidate their state.
//
// Also pins the A-2 gauge contract: the pubsub_active_subscriptions_ gauge is owned by
// DownstreamSubscriber (removeChannel / ~DownstreamSubscriber), NOT recomputed by
// ProxyFilter::onEvent (which only drives the per-registry unsubscribe COUNTER cleanup). clear()
// empties the registry maps but must leave each subscriber's ``subscribed_channels_`` intact, so
// the eventual ~DownstreamSubscriber drains the gauge by that count. The inline lambda below
// asserts the set is still populated at close-time — a proxy for "clear() did not pre-empty it" —
// so a regression that zeroes the set (and thus the later drain, leaking the gauge) fails here.
TEST_F(SubscriptionRegistryTest, ClearClosesAffectedDownstreamConnections) {
  auto sub_a = makeSubscriber();
  auto sub_b = makeSubscriber();
  // Two subscribers on distinct sharded channels — verifies clear() closes each distinct
  // subscriber.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("sch-a", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"sch-a"}, sub_a);
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("sch-b", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"sch-b"}, sub_b);

  // Snapshot pre-clear sizes for the close-time assertion below.
  ASSERT_EQ(1, sub_a->subscribedChannels().size());
  ASSERT_EQ(1, sub_b->subscribedChannels().size());

  // Each connection close must see its channel entry still present: clear() must NOT pre-empty the
  // subscriber's set, or the later ~DownstreamSubscriber drain would decrement the gauge by 0 and
  // leak it (A-2). ProxyFilter::onEvent does not touch the gauge here.
  EXPECT_CALL(*connections_[0], close(Network::ConnectionCloseType::NoFlush))
      .WillOnce(Invoke(
          [&](Network::ConnectionCloseType) { EXPECT_EQ(1, sub_a->subscribedChannels().size()); }));
  EXPECT_CALL(*connections_[1], close(Network::ConnectionCloseType::NoFlush))
      .WillOnce(Invoke(
          [&](Network::ConnectionCloseType) { EXPECT_EQ(1, sub_b->subscribedChannels().size()); }));
  registry_.clear();

  EXPECT_TRUE(registry_.empty());
}

// clear() must drop pending_subscribe_acks_ so an upstream subscribe-ack arriving after the
// registry was torn down does not fabricate an ack frame onto a stale subscriber.
TEST_F(SubscriptionRegistryTest, ClearDropsPendingSubscribeAcks) {
  auto sub = makeSubscriber();
  // Issue a subscribe whose upstream send "succeeds" — this populates pending_subscribe_acks_
  // with an entry waiting for the upstream subscribe-ack Push to arrive.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  EXPECT_CALL(*connections_[0], close(Network::ConnectionCloseType::NoFlush));
  registry_.clear();

  // Drive the would-be subscribe-ack now. With pending_subscribe_acks_ cleared this is a
  // silent drop (the channel is no longer in any map either); the test fails if it instead
  // tries to deliver to the (closed) connection.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_);

  EXPECT_TRUE(registry_.empty());
}

// E-7: a subscriber's disconnect drops only ITS OWN parked pending acks. dropPendingForSubscriber
// sweeps the (small, ≤10s-window) pending map and scrubs just this subscriber from each bucket
// (S-5: no per-subscriber key mirror), so another subscriber's pending ack on a different channel
// must survive and still deliver when its upstream ack arrives.
TEST_F(SubscriptionRegistryTest, DropPendingForSubscriberLeavesOtherSubscribersPending) {
  auto sub_a = makeSubscriber();
  auto sub_b = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("cha", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("chb", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"cha"}, sub_a); // parks a pending ack for "cha" under sub_a
  registry_.subscribe({"chb"}, sub_b); // parks a pending ack for "chb" under sub_b

  // sub_a disconnects: its "cha" pending ack is dropped; "chb" (sub_b's) is untouched.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("cha", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.removeSubscriber(sub_a);

  // The upstream ack for "cha" now finds no pending entry — nothing delivered to sub_a.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "cha", 1), mock_host_);

  // sub_b's "chb" pending ack survived and delivers on its upstream ack.
  EXPECT_CALL(*connections_[1],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$3\r\nchb\r\n:1\r\n"), false));
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "chb", 1), mock_host_);
}

// --- onClusterTopologyChange retry on failure ---

TEST_F(SubscriptionRegistryTest, TopologyChangeRetryOnFailure) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch1"}, sub);

  // Topology change: ``SUNSUBSCRIBE`` succeeds, ``SSUBSCRIBE`` fails -> triggers retry. A genuine
  // owner change makes the reroute fire (G4: a null resolve now leaves the channel put).
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch1"))
      .WillRepeatedly(Return(mock_host_2_));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe("ch1", _))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  // The reroute sends to the dry-resolved host via sendUpstreamSsubscribeToHost (E-8); make it fail
  // to exercise the retry path (overrides the fixture's success default).
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribeToHost("ch1", _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onClusterTopologyChange();
}

// --- onPushMessage with non-string channel element ---

TEST_F(SubscriptionRegistryTest, PushMessageNonStringChannelIgnored) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch1"}, sub);

  // Push with integer channel element instead of string.
  auto push = std::make_unique<Common::Redis::RespValue>();
  push->type(Common::Redis::RespType::Push);
  std::vector<Common::Redis::RespValue> arr(3);
  arr[0].type(Common::Redis::RespType::BulkString);
  arr[0].asString() = "smessage";
  arr[1].type(Common::Redis::RespType::Integer); // wrong type
  arr[1].asInteger() = 42;
  arr[2].type(Common::Redis::RespType::BulkString);
  arr[2].asString() = "data";
  push->asArray().swap(arr);

  // Should log warning and not crash; no delivery to subscriber.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  EXPECT_NO_THROW(registry_.onPushMessage(std::move(push), mock_host_));
}

// --- DownstreamSubscriber deliver on closed connection ---

TEST_F(SubscriptionRegistryTest, DeliverOnClosedConnectionNoOp) {
  auto sub = makeSubscriber();
  // Override the connection state to Closed.
  EXPECT_CALL(*connections_[0], state()).WillRepeatedly(Return(Network::Connection::State::Closed));

  // Should not write to connection.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  sub->deliver(*makeMessagePush("message", "ch1", "data"));
}

// --- doResubscribe when empty (early exit) ---

TEST_F(SubscriptionRegistryTest, DoResubscribeWhenEmptyStopsRetry) {
  auto subscriber = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch1"}, subscriber);

  // Trigger connection close — schedules resubscribe.
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_));
  registry_.onUpstreamConnectionClose();

  // Clear all subscriptions before the timer fires.
  registry_.clear();

  // doResubscribe (what the pool's timer drives) should be a no-op since the registry is empty.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _)).Times(0);
  registry_.doResubscribe();
}

// --- Malformed message push (< 3 elements) ---

TEST_F(SubscriptionRegistryTest, MalformedMessagePushTooFewElements) {
  auto subscriber = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch1", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch1"}, subscriber);

  auto push = std::make_unique<Common::Redis::RespValue>();
  push->type(Common::Redis::RespType::Push);
  std::vector<Common::Redis::RespValue> arr(2); // only 2 elements, need 3
  arr[0].type(Common::Redis::RespType::BulkString);
  arr[0].asString() = "smessage";
  arr[1].type(Common::Redis::RespType::BulkString);
  arr[1].asString() = "ch1";
  push->asArray().swap(arr);

  // Should log warning and not crash or deliver.
  EXPECT_CALL(*connections_[0], write(_, _)).Times(0);
  registry_.onPushMessage(std::move(push), mock_host_);
}

// --- Round-4 full-review regression tests ---

// A1-f1: removeSubscriber must SKIP a channel that is present in THIS registry's subscriptions_ but
// whose set does NOT contain this subscriber — a multi-registry subscriber whose channel is owned
// here by a DIFFERENT subscriber (or re-homed to a sibling registry on a listener re-config).
// Without the ``erase == 0`` guard it decrements the shared subscribed_channels_ / gauge for a
// reference it never held here, stranding the real owner. Symmetric with removeSubscriptions'
// guard.
TEST_F(SubscriptionRegistryTest, RemoveSubscriberSkipsChannelItDoesNotOwnForThisSubscriber) {
  NiceMock<MockUpstreamSubscriptionCallbacks> other_callbacks;
  SubscriptionRegistry other_registry(other_callbacks, random_, dispatcher_);

  auto sub_here = makeSubscriber();  // will own "ch" on registry_
  auto sub_other = makeSubscriber(); // will own "ch" on other_registry (same channel NAME)

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub_here);
  EXPECT_CALL(other_callbacks, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  other_registry.subscribe({"ch"}, sub_other);
  EXPECT_EQ(1, sub_other->totalSubscriptionCount());

  // ProxyFilter::onEvent walks registry_ on sub_other's disconnect. registry_ owns "ch" (via
  // sub_here), but sub_other is NOT in that channel's set: the guard skips it. No SUNSUBSCRIBE
  // fires on registry_, and sub_other's real reference (on other_registry) is left intact rather
  // than being over-decremented to 0 and stranded.
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe(_, _)).Times(0);
  registry_.removeSubscriber(sub_other);

  EXPECT_EQ(1,
            sub_other->totalSubscriptionCount()); // not over-decremented by the non-owning registry
  EXPECT_EQ(1, registry_.subscriptionCount());    // sub_here still owns "ch" here
}

// E-1: when the backoff doResubscribe re-resolves a channel to a DIFFERENT host than its kept owner
// (D1), the old owner still carries the upstream shard subscription. reissueSsubscribe must pair
// the reroute exactly like a topology change — forget the old owner and send it a courtesy
// SUNSUBSCRIBE (find-only, harmless if that connection is already gone) — before recording the new
// owner. Without it the old host keeps a leaked subscription: duplicate delivery + a forever-idle
// connection on a standalone / ring-hash upstream that has no Redis migration invalidation.
TEST_F(SubscriptionRegistryTest, ReissueToDifferentHostSunsubscribesOldOwner) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()))   // initial subscribe -> A
      .WillOnce(Invoke(ssubscribeReturnsHost2())); // doResubscribe re-resolves -> B (slot moved)
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // A's subscription connection drops: ch enters the resubscribe scope with its owner (A) KEPT
  // (D1).
  registry_.onUpstreamConnectionClose(mock_host_);

  // doResubscribe re-resolves ch to B; the kept owner A differs, so A is forgotten AND sent a
  // courtesy SUNSUBSCRIBE before B is recorded.
  EXPECT_CALL(upstream_callbacks_,
              sendUpstreamSunsubscribe("ch", Upstream::HostConstSharedPtr(mock_host_)))
      .WillOnce(Return(SunsubscribeResult::NotSent));
  registry_.doResubscribe();

  ASSERT_EQ(1, registry_.channelHosts().count("ch"));
  EXPECT_EQ(mock_host_2_, registry_.channelHosts().at("ch").host); // now owned by B
}

// B-1: a burst of re-subscribe signals in one dispatch cycle COALESCES onto the single
// already-armed backoff timer. The first signal arms; while the pool timer is still pending
// (resubscribeTimerPending() == true) later signals ride it — no per-signal backoff advance, no
// re-arm. Otherwise a K-signal burst would jump the FIRST retry from ~100ms toward the 30s cap and
// each enableTimer would push the pending deadline out (A1-f2's defer).
TEST_F(SubscriptionRegistryTest, ResubscribeSignalBurstCoalescesOntoOnePendingTimer) {
  ON_CALL(random_, random()).WillByDefault(Return(99999999ULL)); // jitter reveals the interval (99)
  std::vector<int64_t> delays;
  ON_CALL(upstream_callbacks_, scheduleResubscribe(_))
      .WillByDefault(
          Invoke([&delays](std::chrono::milliseconds d) { delays.push_back(d.count()); }));

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe(_, _))
      .WillRepeatedly(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);

  // First signal sees no cycle pending -> arms at the floor (interval 100 -> delay 99). Later
  // signals in the same cycle see the pool timer still armed -> coalesce (no advance, no re-arm).
  EXPECT_CALL(upstream_callbacks_, resubscribeTimerPending())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  registry_.onUpstreamConnectionClose(mock_host_);
  registry_.onUpstreamConnectionClose(mock_host_);
  registry_.onUpstreamConnectionClose(mock_host_);

  EXPECT_THAT(delays, testing::ElementsAre(99)); // one arm at the floor — not three escalating arms
}

// A1-f2: a no-reroute topology change (every active channel's owner unchanged — the common
// slot-only rebalance) must NOT arm the resubscribe-generation timer over channels an UNRELATED
// prior signal parked in the retry scope. Asserted via the timer-creation count: only the
// subscribe-ack timer is ever created; arming a generation timer here would be a SECOND createTimer
// (which the OLD code did, deferring the pending pool cycle on timeout).
TEST_F(SubscriptionRegistryTest, TopologyChangeWithNoRerouteDoesNotArmGenerationTimer) {
  EXPECT_CALL(dispatcher_, createTimer_(_))
      .Times(1)
      .WillOnce(
          Invoke([](Event::TimerCb) -> Event::Timer* { return new NiceMock<Event::MockTimer>(); }));

  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub); // createTimer #1: the shared subscribe-ack timer
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // A prior UNRELATED connection loss parks ch in the retry scope (owner A KEPT, D1); the pool
  // backoff is scheduled but doResubscribe has not run yet.
  registry_.onUpstreamConnectionClose(mock_host_);

  // An unrelated rebalance fires a topology change where ch's owner is UNCHANGED -> ch is skipped,
  // nothing is reissued -> no generation timer is armed (no second createTimer).
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch")).WillOnce(Return(mock_host_));
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSunsubscribe(_, _)).Times(0);
  registry_.onClusterTopologyChange();
}

// A1-f3: a -MOVED reply correlated to a SUNSUBSCRIBE (not an SSUBSCRIBE) is STRUCTURALLY always
// stale — we forget a channel's owner BEFORE sending its SUNSUBSCRIBE, so the redirect only
// confirms the slot moved. It must refresh the topology but must NOT re-mark this host's healthy
// channels for re-subscribe (which would needlessly re-SSUBSCRIBE them and open a duplicate/gap
// window). Symmetric with the generic-error branch's owner check.
TEST_F(SubscriptionRegistryTest, SunsubscribeCorrelatedMovedRefreshesButDoesNotResubscribeHost) {
  auto sub = makeSubscriber();
  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("ch", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"ch"}, sub);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "ch", 1), mock_host_); // active on A

  // Topology reroute A -> B: SUNSUBSCRIBE to A (recorded at A's FIFO head), SSUBSCRIBE to B.
  EXPECT_CALL(upstream_callbacks_, chooseUpstreamHostForChannel("ch"))
      .WillOnce(Return(mock_host_2_));
  EXPECT_CALL(upstream_callbacks_,
              sendUpstreamSunsubscribe("ch", Upstream::HostConstSharedPtr(mock_host_)))
      .WillOnce(Return(SunsubscribeResult::AckExpected));
  registry_.onClusterTopologyChange();

  // A answers the SUNSUBSCRIBE with -MOVED (it gave up the slot). Refresh the topology, but do NOT
  // schedule a whole-host re-subscribe: this redirect is not a current SSUBSCRIBE attempt.
  EXPECT_CALL(upstream_callbacks_, requestTopologyRefresh());
  EXPECT_CALL(upstream_callbacks_, scheduleResubscribe(_)).Times(0);
  auto err = std::make_unique<Common::Redis::RespValue>();
  err->type(Common::Redis::RespType::Error);
  err->asString() = "MOVED 1234 127.0.0.1:6380";
  registry_.onUpstreamControlError(std::move(err), mock_host_);
}

// SW-4: a duplicate pipelined ``SUBSCRIBE dup dup`` parks TWO entries for the same (subscriber,
// channel). When the upstream ack lands, real Redis delivers an ack PER entry (delivery stays
// per-entry), but the SUCCESS counter must fire ONCE per (subscriber, channel) — symmetric with the
// error path's MISC-2 gate — so success/error stay in the same unit and a success-rate dashboard
// does not drift under duplicates.
TEST_F(SubscriptionRegistryTest, DuplicateSubscribeSuccessCountsAckSuccessOnce) {
  Stats::IsolatedStoreImpl store;
  Stats::Counter& success = store.rootScope()->counterFromString("pubsub_subscribe_ack_success");
  Stats::Counter& error = store.rootScope()->counterFromString("pubsub_subscribe_ack_error");
  auto sub = makeSubscriberWithAckCounters(success, error);

  EXPECT_CALL(upstream_callbacks_, sendUpstreamSsubscribe("dup", _))
      .WillOnce(Invoke(ssubscribeReturnsHost()));
  registry_.subscribe({"dup"}, sub);
  registry_.subscribe({"dup"}, sub); // dedups onto the same still-pending bucket -> two entries

  // Upstream acks: an ack is DELIVERED per entry (two writes — Redis-compatible), but success
  // counts ONCE (only the newly_added entry).
  EXPECT_CALL(*connections_[0],
              write(BufferString(">3\r\n$9\r\nsubscribe\r\n$3\r\ndup\r\n:1\r\n"), false))
      .Times(2);
  registry_.onPushMessage(makeSubscribeAckPush("ssubscribe", "dup", 1), mock_host_);
  EXPECT_EQ(1U, success.value()); // SW-4: once, not twice
  EXPECT_EQ(0U, error.value());
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
