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
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Http {
namespace {

class ConnPoolImplForTest : public Event::TestUsingSimulatedTime, public HttpConnPoolImplMixed {
public:
  ConnPoolImplForTest(Event::MockDispatcher& dispatcher, Upstream::ClusterConnectivityState& state,
                      Random::RandomGenerator& random, Upstream::ClusterInfoConstSharedPtr cluster,
                      HttpServerPropertiesCache::Origin origin,
                      HttpServerPropertiesCacheSharedPtr cache)
      : HttpConnPoolImplMixed(
            dispatcher, random, Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000", simTime()),
            Upstream::ResourcePriority::Default, nullptr, nullptr, state, origin, cache) {}
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
                                                         origin_, cache_)) {}

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
  std::unique_ptr<ConnPoolImplForTest> conn_pool_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Event::MockSchedulableCallback>* mock_upstream_ready_cb_;

  void testAlpnHandshake(absl::optional<Protocol> protocol);
  TestScopedRuntime scoped_runtime;
  // The default capacity for HTTP/2 streams.
  uint32_t expected_capacity_{536870912};
};

TEST_F(MixedConnPoolImplTest, AlpnTest) {
  auto& fallback = conn_pool_->transportSocketOptions()->applicationProtocolFallback();
  ASSERT_EQ(2, fallback.size());
  EXPECT_EQ(fallback[0], Http::Utility::AlpnNames::get().Http2);
  EXPECT_EQ(fallback[1], Http::Utility::AlpnNames::get().Http11);
}

void MixedConnPoolImplTest::testAlpnHandshake(absl::optional<Protocol> protocol) {
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
  conn_pool_.reset();
}

TEST_F(MixedConnPoolImplTest, BasicNoAlpnHandshakeNew) { testAlpnHandshake({}); }

TEST_F(MixedConnPoolImplTest, HandshakeWithCachedLimit) {
  expected_capacity_ = 5;
  EXPECT_CALL(mock_cache_, getConcurrentStreams(_)).WillOnce(Return(5));
  testAlpnHandshake({});
}

TEST_F(MixedConnPoolImplTest, HandshakeWithCachedLimitCapped) {
  EXPECT_CALL(mock_cache_, getConcurrentStreams(_))
      .WillOnce(Return(std::numeric_limits<uint32_t>::max()));
  testAlpnHandshake({});
}

TEST_F(MixedConnPoolImplTest, Http1AlpnHandshake) { testAlpnHandshake(Protocol::Http11); }

TEST_F(MixedConnPoolImplTest, Http2AlpnHandshake) { testAlpnHandshake(Protocol::Http2); }

} // namespace
} // namespace Http
} // namespace Envoy
