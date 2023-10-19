#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class CdsIntegrationTest : public XdsIntegrationTest {
public:
  CdsIntegrationTest() {
    use_lds_ = false;
    default_request_headers_.setScheme("http");
  }

  void createEnvoy() override {
    sotw_or_delta_ = sotwOrDelta();
    const std::string target_uri = Network::Test::getLoopbackAddressUrlString(ipVersion());
    Platform::XdsBuilder xds_builder(target_uri, fake_upstreams_[1]->localAddress()->ip()->port());
    std::string cds_resources_locator;
    if (use_xdstp_) {
      cds_namespace_ = "xdstp://" + target_uri + "/envoy.config.cluster.v3.Cluster";
      cds_resources_locator = cds_namespace_ + "/*";
    }
    xds_builder.addClusterDiscoveryService(cds_resources_locator, /*timeout_in_seconds=*/1);
    builder_.setXds(std::move(xds_builder));

    XdsIntegrationTest::createEnvoy();
  }

  void SetUp() override { setUpstreamProtocol(Http::CodecType::HTTP1); }

protected:
  void executeCdsRequestsAndVerify() {
    initialize();
    const std::string cluster_name =
        use_xdstp_ ? cds_namespace_ + "/my_cluster?xds.node.cluster=envoy-mobile" : "my_cluster";
    envoy::config::cluster::v3::Cluster cluster1 = ConfigHelper::buildStaticCluster(
        cluster_name, fake_upstreams_[0]->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));
    initializeXdsStream();
    int cluster_count = getGaugeValue("cluster_manager.active_clusters");
    // Do the initial compareDiscoveryRequest / sendDiscoveryResponse for cluster_1.
    std::vector<std::string> expected_resources;
    if (use_xdstp_) {
      expected_resources.push_back(cds_namespace_ + "/*");
    }
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", expected_resources, {},
                                        {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                               {cluster1}, {cluster1}, {}, "55");
    // Wait for cluster to be added
    ASSERT_TRUE(waitForCounterGe("cluster_manager.cluster_added", 1));
    ASSERT_TRUE(waitForGaugeGe("cluster_manager.active_clusters", cluster_count + 1));
    ASSERT_TRUE(waitForGaugeGe("cluster_manager.updated_clusters", 0));
    ASSERT_TRUE(waitForGaugeGe("cluster_manager.cluster_removed", 0));
  }

  bool use_xdstp_{false};
  std::string cds_namespace_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeSotw, CdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // Envoy Mobile's xDS APIs only support state-of-the-world, not delta.
                     testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::UnifiedSotw)));

TEST_P(CdsIntegrationTest, Basic) { executeCdsRequestsAndVerify(); }

TEST_P(CdsIntegrationTest, BasicWithXdstp) {
  use_xdstp_ = true;
  executeCdsRequestsAndVerify();
}

} // namespace
} // namespace Envoy
