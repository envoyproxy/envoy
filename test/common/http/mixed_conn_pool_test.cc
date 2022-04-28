#include <memory>

#include "source/common/http/mixed_conn_pool.h"
#include "source/common/http/utility.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"
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
                      Random::RandomGenerator& random, Upstream::ClusterInfoConstSharedPtr cluster)
      : HttpConnPoolImplMixed(dispatcher, random,
                              Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000", simTime()),
                              Upstream::ResourcePriority::Default, nullptr, nullptr, state) {}
};

/**
 * Test fixture for a connection pool which can have HTTP/2 or HTTP/1.1 connections.
 */
class MixedConnPoolImplTest : public testing::Test {
public:
  MixedConnPoolImplTest()
      : upstream_ready_cb_(new NiceMock<Event::MockSchedulableCallback>(&dispatcher_)),
        conn_pool_(std::make_unique<ConnPoolImplForTest>(dispatcher_, state_, random_, cluster_)) {}

  ~MixedConnPoolImplTest() override {
    EXPECT_EQ("", TestUtility::nonZeroedGauges(cluster_->stats_store_.gauges()));
  }

  Upstream::ClusterConnectivityState state_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb_;
  std::unique_ptr<ConnPoolImplForTest> conn_pool_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Event::MockSchedulableCallback>* mock_upstream_ready_cb_;

  void testAlpnHandshake(absl::optional<Protocol> protocol);
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
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.allow_concurrency_for_alpn_pool")) {
    EXPECT_EQ(536870912, state_.connecting_and_connected_stream_capacity_);
  } else {
    EXPECT_EQ(1, state_.connecting_and_connected_stream_capacity_);
  }

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

TEST_F(MixedConnPoolImplTest, BasicNoAlpnHandshake) { testAlpnHandshake({}); }

TEST_F(MixedConnPoolImplTest, Http1AlpnHandshake) { testAlpnHandshake(Protocol::Http11); }

TEST_F(MixedConnPoolImplTest, Http2AlpnHandshake) { testAlpnHandshake(Protocol::Http2); }

} // namespace
} // namespace Http
} // namespace Envoy
