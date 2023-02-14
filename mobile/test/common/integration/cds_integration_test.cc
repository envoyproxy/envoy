#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {
constexpr char ClusterName[] = "newcluster";

class CdsIntegrationTest : public XdsIntegrationTest {
public:
  CdsIntegrationTest() {
    use_lds_ = false;
    default_request_headers_.setScheme("http");
  }

  void createEnvoy() override {
    sotw_or_delta_ = sotwOrDelta();
    const std::string api_type = sotw_or_delta_ == Grpc::SotwOrDelta::Sotw ||
                                         sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedSotw
                                     ? "GRPC"
                                     : "DELTA_GRPC";
    builder_.addCdsLayer(/*timeout_seconds=*/1);
    builder_.setAggregatedDiscoveryService(api_type,
                                           Network::Test::getLoopbackAddressUrlString(ipVersion()),
                                           fake_upstreams_[1]->localAddress()->ip()->port());
    XdsIntegrationTest::createEnvoy();
  }
  void SetUp() override { setUpstreamProtocol(Http::CodecType::HTTP1); }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, CdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(CdsIntegrationTest, Basic) {
  initialize();
  envoy::config::cluster::v3::Cluster cluster1 = ConfigHelper::buildStaticCluster(
      ClusterName, fake_upstreams_[0]->localAddress()->ip()->port(),
      Network::Test::getLoopbackAddressString(ipVersion()), "ROUND_ROBIN");
  initializeXdsStream();
  int cluster_count = getGaugeValue("cluster_manager.active_clusters");
  // Do the initial compareDiscoveryRequest / sendDiscoveryResponse for cluster_1.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {cluster1}, {cluster1}, {}, "55");
  // Wait for cluster to be added
  ASSERT_TRUE(waitForCounterGe("cluster_manager.cluster_added", 1));
  ASSERT_TRUE(waitForGaugeGe("cluster_manager.active_clusters", cluster_count + 1));
}

} // namespace
} // namespace Envoy
