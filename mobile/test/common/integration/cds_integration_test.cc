#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using envoy::config::cluster::v3::Cluster;

class CdsIntegrationTest : public XdsIntegrationTest {
public:
  void initialize() override {
    setUpstreamProtocol(Http::CodecType::HTTP1);

    XdsIntegrationTest::initialize();

    default_request_headers_.setScheme("http");
    initializeXdsStream();
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
    xds_builder.addClusterDiscoveryService(cds_resources_locator, /*timeout_in_seconds=*/1)
        .setSslRootCerts(getUpstreamCert());
    builder_.setXds(std::move(xds_builder));

    XdsIntegrationTest::createEnvoy();
  }

  void SetUp() override { initialize(); }

protected:
  Cluster createCluster() {
    const std::string cluster_name =
        use_xdstp_ ? cds_namespace_ + "/my_cluster?xds.node.cluster=envoy-mobile" : "my_cluster";
    return ConfigHelper::buildStaticCluster(cluster_name,
                                            fake_upstreams_[0]->localAddress()->ip()->port(),
                                            Network::Test::getLoopbackAddressString(ipVersion()));
  }

  std::vector<std::string> getExpectedResources() {
    std::vector<std::string> expected_resources;
    if (use_xdstp_) {
      expected_resources.push_back(cds_namespace_ + "/*");
    }
    return expected_resources;
  }

  void sendInitialCdsResponseAndVerify(const std::string& version) {
    const int cluster_count = getGaugeValue("cluster_manager.active_clusters");
    const std::vector<std::string> expected_resources = getExpectedResources();

    // Envoy sends the initial DiscoveryRequest.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", expected_resources, {},
                                        {}, /*expect_node=*/true));

    Cluster cluster = createCluster();
    // Server sends back the initial DiscoveryResponse.
    sendDiscoveryResponse<Cluster>(Config::TypeUrl::get().Cluster, {cluster}, {cluster}, {},
                                   version);

    // Wait for cluster to be added.
    EXPECT_TRUE(waitForCounterGe("cluster_manager.cluster_added", 1));
    EXPECT_TRUE(waitForGaugeGe("cluster_manager.active_clusters", cluster_count + 1));

    // ACK of the initial version.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, version, expected_resources,
                                        {}, {}, /*expect_node=*/false));

    EXPECT_TRUE(waitForGaugeGe("cluster_manager.cluster_removed", 0));
  }

  void sendUpdatedCdsResponseAndVerify(const std::string& version) {
    const int cluster_count = getGaugeValue("cluster_manager.active_clusters");
    const std::vector<std::string> expected_resources = getExpectedResources();

    // Server sends an updated DiscoveryResponse over the xDS stream.
    Cluster cluster = createCluster();
    sendDiscoveryResponse<Cluster>(Config::TypeUrl::get().Cluster, {cluster}, {cluster}, {},
                                   version);

    // ACK of the cluster update at the new version.
    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, version, expected_resources,
                                        {}, {}, /*expect_node=*/false));

    // Cluster count should stay the same.
    EXPECT_TRUE(waitForGaugeGe("cluster_manager.active_clusters", cluster_count));
    EXPECT_TRUE(waitForGaugeGe("cluster_manager.cluster_removed", 0));
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

TEST_P(CdsIntegrationTest, Basic) { sendInitialCdsResponseAndVerify(/*version=*/"55"); }

TEST_P(CdsIntegrationTest, BasicWithXdstp) {
  use_xdstp_ = true;
  sendInitialCdsResponseAndVerify(/*version=*/"55");
}

TEST_P(CdsIntegrationTest, ClusterUpdates) {
  use_xdstp_ = true;
  sendInitialCdsResponseAndVerify(/*version=*/"55");
  sendUpdatedCdsResponseAndVerify(/*version=*/"56");
}

} // namespace
} // namespace Envoy
