#include <memory>

#include "source/common/http/mixed_conn_pool.h"
#include "source/common/http/utility.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/http_server_properties_cache.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Property;
using testing::Return;

namespace Envoy {
namespace Http {
namespace {

class ConnPoolImplForTest : public Event::TestUsingSimulatedTime, public HttpConnPoolImplMixed {
public:
  ConnPoolImplForTest(Event::MockDispatcher& dispatcher, Upstream::ClusterConnectivityState& state,
                      Random::RandomGenerator& random, Upstream::ClusterInfoConstSharedPtr cluster,
                      HttpServerPropertiesCache::Origin origin,
                      HttpServerPropertiesCacheSharedPtr cache,
                      Server::OverloadManager& overload_manager)
      : HttpConnPoolImplMixed(dispatcher, random,
                              Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000"),
                              Upstream::ResourcePriority::Default, nullptr, nullptr, state, origin,
                              cache, overload_manager) {}
};

/**
 * Test fixture for a connection pool which can have HTTP/2 or HTTP/1.1 connections.
 */
class MixedConnPoolImplTest : public testing::Test {
public:
  MixedConnPoolImplTest()
      : upstream_ready_cb_(new NiceMock<Event::MockSchedulableCallback>(&dispatcher_)),
        cache_(std::make_shared<NiceMock<MockHttpServerPropertiesCache>>()),
        mock_cache_(*(dynamic_cast<MockHttpServerPropertiesCache*>(cache_.get()))),
        conn_pool_(std::make_unique<ConnPoolImplForTest>(dispatcher_, state_, random_, cluster_,
                                                         origin_, cache_, overload_manager_)) {}

  ~MixedConnPoolImplTest() override {
    EXPECT_EQ("", TestUtility::nonZeroedGauges(cluster_->stats_store_.gauges()));
  }

  Upstream::ClusterConnectivityState state_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb_;
  Http::HttpServerPropertiesCacheSharedPtr cache_;
  MockHttpServerPropertiesCache& mock_cache_;
  HttpServerPropertiesCache::Origin origin_{"https", "hostname.com", 443};
  NiceMock<Server::MockOverloadManager> overload_manager_;
  std::unique_ptr<ConnPoolImplForTest> conn_pool_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Event::MockSchedulableCallback>* mock_upstream_ready_cb_;

  void testAlpnHandshake(std::optional<Protocol> protocol);
  TestScopedRuntime scoped_runtime;
  // The default capacity for HTTP/2 streams. This is determined by both the HTTP2 options
  // (DEFAULT_MAX_CONCURRENT_STREAMS) and DEFAULT_MAX_STREAMS of connection pool.
  uint32_t expected_capacity_{1024};
};

TEST_F(MixedConnPoolImplTest, AlpnTest) {
  auto& fallback = conn_pool_->transportSocketOptions()->applicationProtocolFallback();
  ASSERT_EQ(2, fallback.size());
  EXPECT_EQ(fallback[0], Http::Utility::AlpnNames::get().Http2);
  EXPECT_EQ(fallback[1], Http::Utility::AlpnNames::get().Http11);
}

void MixedConnPoolImplTest::testAlpnHandshake(std::optional<Protocol> protocol) {
  NiceMock<ConnPoolCallbacks> callbacks_;

  auto* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection));
  NiceMock<MockResponseDecoder> decoder;
  conn_pool_->newStream(decoder, callbacks_, {false, true});

  std::string next_protocol = "";
  if (protocol.has_value()) {
    next_protocol = (protocol.value() == Protocol::Http11 ? Http::Utility::AlpnNames::get().Http11
                                                          : Http::Utility::AlpnNames::get().Http2);
  }
  EXPECT_CALL(*connection, nextProtocol()).WillOnce(Return(next_protocol));
  EXPECT_EQ(expected_capacity_, state_.connecting_and_connected_stream_capacity_);

  connection->raiseEvent(Network::ConnectionEvent::Connected);
  if (!protocol.has_value()) {
    EXPECT_EQ(Protocol::Http11, conn_pool_->protocol());
  } else {
    EXPECT_EQ(protocol.value(), conn_pool_->protocol());
  }

  conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  connection->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_total_.value());
  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_destroy_.value());
  EXPECT_EQ(0, cluster_->trafficStats()->upstream_cx_active_.value());
  EXPECT_EQ(cluster_->trafficStats()->upstream_cx_total_.value(),
            cluster_->trafficStats()->upstream_cx_destroy_.value());
  conn_pool_.reset();
}

TEST_F(MixedConnPoolImplTest, BasicNoAlpnHandshakeNew) { testAlpnHandshake({}); }

TEST_F(MixedConnPoolImplTest, HandshakeWithCachedLimit) {
  expected_capacity_ = 5;
  EXPECT_CALL(mock_cache_, getConcurrentStreams(_)).WillOnce(Return(5));
  testAlpnHandshake({});
}

// Test that increasing the limit upon connect, versus what was in the cache before connection,
// works correctly.
TEST_F(MixedConnPoolImplTest, HandshakeWithCachedLimitAndEffectiveIncrease) {
  expected_capacity_ = 1;

  // This simulates a previous connection being http 1.
  EXPECT_CALL(mock_cache_, getConcurrentStreams(_)).WillOnce(Return(1));

  // This makes the new connection http 2, which has more than 1 stream available.
  testAlpnHandshake(Protocol::Http2);
}

TEST_F(MixedConnPoolImplTest, HandshakeWithCachedLimitCapped) {
  EXPECT_CALL(mock_cache_, getConcurrentStreams(_))
      .WillOnce(Return(std::numeric_limits<uint32_t>::max()));
  testAlpnHandshake({});
}

TEST_F(MixedConnPoolImplTest, Http1AlpnHandshake) { testAlpnHandshake(Protocol::Http11); }

TEST_F(MixedConnPoolImplTest, Http2AlpnHandshake) { testAlpnHandshake(Protocol::Http2); }

TEST_F(MixedConnPoolImplTest, HandoverConnectionTimingAndStats) {
  // Verify that upstream_cx_connect_ms and upstream_cx_length_ms are each delivered to sinks
  // exactly once with their true durations.
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_connect_ms"), 15))
      .Times(1);
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_length_ms"), 40))
      .Times(1);
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_rq_per_cx"), _))
      .Times(1);

  NiceMock<ConnPoolCallbacks> callbacks_;
  auto* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection));
  NiceMock<MockResponseDecoder> decoder;
  conn_pool_->newStream(decoder, callbacks_, {false, true});

  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_total_.value());
  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_active_.value());

  EXPECT_CALL(*connection, nextProtocol()).WillOnce(Return(Http::Utility::AlpnNames::get().Http2));

  // Advance time during connection establishment.
  conn_pool_->simTime().setMonotonicTime(conn_pool_->simTime().monotonicTime() +
                                         std::chrono::milliseconds(15));
  connection->raiseEvent(Network::ConnectionEvent::Connected);

  // During handover, active connection count must stay at 1 (not spike to 2) and
  // upstream_cx_total must remain 1 (not double).
  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_total_.value());
  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_active_.value());
  EXPECT_EQ(Protocol::Http2, conn_pool_->protocol());

  // Advance time during HTTP stream processing.
  conn_pool_->simTime().setMonotonicTime(conn_pool_->simTime().monotonicTime() +
                                         std::chrono::milliseconds(25));

  conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  connection->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  // After teardown, upstream_cx_total must equal upstream_cx_destroy, and active must be 0.
  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_total_.value());
  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_destroy_.value());
  EXPECT_EQ(0, cluster_->trafficStats()->upstream_cx_active_.value());
  EXPECT_EQ(cluster_->trafficStats()->upstream_cx_total_.value(),
            cluster_->trafficStats()->upstream_cx_destroy_.value());

  conn_pool_.reset();
}

TEST_F(MixedConnPoolImplTest, LegacyHandoverWhenRuntimeFlagDisabled) {
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.mixed_conn_pool_handover_stats", "false"}});

  NiceMock<ConnPoolCallbacks> callbacks_;
  auto* connection = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection));
  NiceMock<MockResponseDecoder> decoder;
  conn_pool_->newStream(decoder, callbacks_, {false, true});

  EXPECT_CALL(*connection, nextProtocol()).WillOnce(Return(Http::Utility::AlpnNames::get().Http11));
  connection->raiseEvent(Network::ConnectionEvent::Connected);

  // When flag is disabled, legacy behavior increments upstream_cx_total again upon handover.
  EXPECT_EQ(2, cluster_->trafficStats()->upstream_cx_total_.value());

  conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  connection->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(2, cluster_->trafficStats()->upstream_cx_total_.value());
  EXPECT_EQ(1, cluster_->trafficStats()->upstream_cx_destroy_.value());
  EXPECT_NE(cluster_->trafficStats()->upstream_cx_total_.value(),
            cluster_->trafficStats()->upstream_cx_destroy_.value());

  conn_pool_.reset();
}

} // namespace
} // namespace Http
} // namespace Envoy
