#include <memory>
#include <utility>

#include "envoy/extensions/upstreams/http/reverse_tunnel/v3/reverse_tunnel_codec.pb.h"
#include "envoy/http/client_codec_factory.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/upstreams/http/reverse_tunnel/config.h"
#include "source/extensions/upstreams/http/reverse_tunnel/drain_aware_client_connection.h"
#include "source/extensions/upstreams/http/reverse_tunnel/drain_registry.h"
#include "source/extensions/upstreams/http/reverse_tunnel/reverse_tunnel_codec_stats.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_info.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {
namespace {

using ::testing::_;
using ::testing::NiceMock;

envoy::extensions::upstreams::http::reverse_tunnel::v3::ReverseTunnelUpstreamCodecOptions
makeProto(bool enable) {
  envoy::extensions::upstreams::http::reverse_tunnel::v3::ReverseTunnelUpstreamCodecOptions proto;
  proto.set_enable_drain_with_goaway(enable);
  return proto;
}

class ReverseTunnelUpstreamCodecTest : public testing::Test {
protected:
  Envoy::Http::ClientCodecFactory::Context makeContext(Envoy::Http::CodecType type) {
    return Envoy::Http::ClientCodecFactory::Context{
        type, connection_, callbacks_, cluster_, random_, transport_socket_options_};
  }

  // store_ must be declared before stats_ (stats_ is generated from its scope).
  Stats::IsolatedStoreImpl store_;
  ReverseTunnelUpstreamCodecStats stats_{
      ReverseTunnelUpstreamCodecStats::generate(*store_.rootScope())};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Envoy::Http::MockConnectionCallbacks> callbacks_;
  NiceMock<Upstream::MockClusterInfo> cluster_;
  NiceMock<Random::MockRandomGenerator> random_;
  std::shared_ptr<const Network::TransportSocketOptions> transport_socket_options_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
};

// The options object must be recoverable as an Http::ClientCodecFactory via sidecast, which is how
// The options object surfaces a per-cluster upstream codec factory via the ProtocolOptionsConfig
// hook (how ClusterInfoImpl discovers it).
TEST_F(ReverseTunnelUpstreamCodecTest, OptionsExposesCodecFactory) {
  Upstream::ProtocolOptionsConfigConstSharedPtr opts =
      std::make_shared<ReverseTunnelUpstreamCodecOptions>(makeProto(true), stats_, nullptr);
  EXPECT_TRUE(opts->upstreamHttpClientCodecFactory().has_value());
}

// Scenario 1/2: a received GOAWAY is observed (logged + counted) and forwarded to the real
// callbacks so the pool's normal drain handling still runs.
TEST_F(ReverseTunnelUpstreamCodecTest, CallbacksObserveAndForwardGoaway) {
  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  DrainAwareClientCallbacks wrapper(inner, stats_);

  EXPECT_CALL(inner, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Envoy::Http::GoAwayErrorCode::NoError);
  EXPECT_EQ(1, stats_.goaway_received_.value());
}

// The decorator forwards ClientConnection calls and counts a sent GOAWAY (scenario 3 hook).
TEST_F(ReverseTunnelUpstreamCodecTest, ConnectionForwardsAndCountsGoawaySent) {
  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, stats_);
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   /*registry=*/nullptr, /*cluster=*/"");

  EXPECT_CALL(*inner_raw, goAway());
  codec.goAway();
  EXPECT_EQ(1, stats_.goaway_sent_.value());

  EXPECT_CALL(*inner_raw, shutdownNotice());
  codec.shutdownNotice();
}

// Scenario 3: graceful drain sends a shutdown notice immediately, then after drain_time sends the
// final GOAWAY to the peer AND gracefully drains the local pool connection (drives onGoAway into
// the pool's active client) instead of hard-closing it. The pool drain is what lets in-flight
// requests finish and routes new requests onto the replacement tunnel; a hard close would abort
// in-flight.
TEST_F(ReverseTunnelUpstreamCodecTest, StartGracefulDrainTwoPhase) {
  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, stats_);
  // MockTimer registers with the dispatcher and captures the timer callback for invokeCallback().
  auto* drain_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   /*registry=*/nullptr, /*cluster=*/"");

  // Phase 1: shutdown notice now, GOAWAY deferred (no goaway_sent yet).
  EXPECT_CALL(*inner_raw, shutdownNotice());
  EXPECT_CALL(*drain_timer, enableTimer(std::chrono::milliseconds(5000), _));
  codec.startGracefulDrain(std::chrono::milliseconds(5000));
  EXPECT_EQ(0, stats_.goaway_sent_.value());

  // Phase 2: timer fires -> final GOAWAY to the peer, plus a graceful pool drain driven into the
  // local pool's active client (onGoAway on the wrapped callbacks). goaway_received_ stays 0
  // because this is a locally-initiated drain, not an observed peer GOAWAY.
  EXPECT_CALL(*inner_raw, goAway());
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  drain_timer->invokeCallback();
  EXPECT_EQ(1, stats_.goaway_sent_.value());
  EXPECT_EQ(0, stats_.goaway_received_.value());

  // Idempotent: a second call does nothing.
  codec.startGracefulDrain(std::chrono::milliseconds(5000));
}

// Scenario 3: the registry fans a drain out to a registered codec for the matching cluster, which
// triggers the two-phase graceful drain (shutdownNotice now, GOAWAY after drain_time).
TEST_F(ReverseTunnelUpstreamCodecTest, RegistryDrainsRegisteredCluster) {
  auto registry = std::make_shared<UpstreamCodecDrainRegistry>(tls_);

  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, stats_);
  auto* drain_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  // Constructing with the registry auto-registers under "cluster_a".
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   registry, "cluster_a");

  // Draining a different cluster is a no-op for this codec.
  registry->drainCluster("other", std::chrono::milliseconds(1000));

  // Draining the matching cluster reaches the codec -> shutdownNotice now.
  EXPECT_CALL(*inner_raw, shutdownNotice());
  registry->drainCluster("cluster_a", std::chrono::milliseconds(1000));

  // Timer fires -> final GOAWAY + graceful pool drain (onGoAway into the local pool), not a close.
  EXPECT_CALL(*inner_raw, goAway());
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  drain_timer->invokeCallback();
  EXPECT_EQ(1, stats_.goaway_sent_.value());
  EXPECT_EQ(0, stats_.goaway_received_.value());
}

// When disabled, the factory declines (returns nullptr) so CodecClientProd uses the stock codec.
TEST_F(ReverseTunnelUpstreamCodecTest, PassThroughWhenDisabled) {
  ReverseTunnelUpstreamCodecOptions opts(makeProto(false), stats_, nullptr);
  EXPECT_EQ(opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP2)), nullptr);
}

// Non-HTTP/2 codecs are not customized even when enabled; the factory declines.
TEST_F(ReverseTunnelUpstreamCodecTest, PassThroughForHttp1) {
  ReverseTunnelUpstreamCodecOptions opts(makeProto(true), stats_, nullptr);
  EXPECT_EQ(opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP1)), nullptr);
}

// Even enabled + HTTP/2, the codec is only customized for reverse-connection clusters. A cluster
// whose type is not envoy.clusters.reverse_connection (here: the default mock returns no custom
// cluster type) declines, falling through to the stock codec.
TEST_F(ReverseTunnelUpstreamCodecTest, PassThroughForNonReverseConnectionCluster) {
  ReverseTunnelUpstreamCodecOptions opts(makeProto(true), stats_, nullptr);

  // MockClusterInfo::clusterType() returns an empty OptRef by default (a built-in cluster type),
  // so the reverse-connection guard does not match.
  ON_CALL(cluster_, clusterType())
      .WillByDefault(
          testing::Return(OptRef<const envoy::config::cluster::v3::Cluster::CustomClusterType>{}));

  EXPECT_EQ(opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP2)), nullptr);
}

} // namespace
} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
