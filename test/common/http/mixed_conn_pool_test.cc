#include <memory>

#include "common/http/conn_pool_base.h"
#include "common/http/utility.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace {

// TODO(alyssawilk) replace this with the MixedConnectionPool once it lands.
class ConnPoolImplForTest : public Event::TestUsingSimulatedTime, public HttpConnPoolImplBase {
public:
  ConnPoolImplForTest(Event::MockDispatcher& dispatcher, Upstream::ClusterConnectivityState& state,
                      Random::RandomGenerator& random, Upstream::ClusterInfoConstSharedPtr cluster)
      : HttpConnPoolImplBase(Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000", simTime()),
                             Upstream::ResourcePriority::Default, dispatcher, nullptr, nullptr,
                             random, state, {Http::Protocol::Http2, Http::Protocol::Http11}) {}

  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override { return nullptr; }
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData&) override {
    return nullptr;
  }
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
};

TEST_F(MixedConnPoolImplTest, AlpnTest) {
  auto& fallback = conn_pool_->transportSocketOptions()->applicationProtocolFallback();
  ASSERT_EQ(2, fallback.size());
  EXPECT_EQ(fallback[0], Http::Utility::AlpnNames::get().Http2);
  EXPECT_EQ(fallback[1], Http::Utility::AlpnNames::get().Http11);
}

} // namespace
} // namespace Http
} // namespace Envoy
