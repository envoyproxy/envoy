#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/config/validators/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

const uint32_t InitialUpstreamIndex = 1;

class DynamicModuleConfigValidatorIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest,
                                                    public HttpIntegrationTest {
public:
  DynamicModuleConfigValidatorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion(),
                            ConfigHelper::discoveredClustersBootstrap(
                                sotwOrDelta() == Grpc::SotwOrDelta::Sotw ||
                                        sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw
                                    ? "GRPC"
                                    : "DELTA_GRPC")) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                      (sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw ||
                                       sotwOrDelta() == Grpc::SotwOrDelta::UnifiedDelta)
                                          ? "true"
                                          : "false");
    use_lds_ = false;
    sotw_or_delta_ = sotwOrDelta();
  }

  void TearDown() override {
    if (!test_skipped_) {
      cleanUpXdsConnection();
    }
  }

  void initializeTest(uint32_t initial_clusters_num) {
    use_lds_ = false;
    test_skipped_ = false;
    setUpstreamCount(1);
    setUpstreamProtocol(Http::CodecType::HTTP2);
    defer_listener_finalization_ = true;
    HttpIntegrationTest::initialize();

    for (uint32_t i = 0; i < initial_clusters_num; ++i) {
      addFakeUpstream(Http::CodecType::HTTP2);
      const std::string cluster_name = absl::StrCat("cluster_", i);
      auto cluster = ConfigHelper::buildStaticCluster(
          cluster_name, fake_upstreams_[InitialUpstreamIndex + i]->localAddress()->ip()->port(),
          Network::Test::getLoopbackAddressString(ipVersion()));
      clusters_.emplace_back(cluster);
    }

    acceptXdsConnection();

    EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "", {}, {}, {}, true));
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster,
                                                               clusters_, clusters_, {}, "7");

    test_server_->waitForGauge("cluster_manager.active_clusters",
                               testing::Ge(initial_clusters_num + 1));
    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"http"});
  }

  void verifyGrpcServiceMethod() {
    EXPECT_TRUE(xds_stream_->waitForHeadersComplete());
    Envoy::Http::LowerCaseString path_string(":path");
    std::string expected_method(
        sotwOrDelta() == Grpc::SotwOrDelta::Sotw || sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw
            ? "/envoy.service.cluster.v3.ClusterDiscoveryService/StreamClusters"
            : "/envoy.service.cluster.v3.ClusterDiscoveryService/DeltaClusters");
    EXPECT_EQ(xds_stream_->headers().get(path_string)[0]->value(), expected_method);
  }

  void acceptXdsConnection() {
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
    verifyGrpcServiceMethod();
  }

  void addValidator(absl::string_view extension_name, absl::string_view extension_config_value) {
    config_helper_.addConfigModifier([extension_name = std::string(extension_name),
                                      extension_config_value = std::string(extension_config_value)](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* config_validator_config = bootstrap.mutable_dynamic_resources()
                                          ->mutable_cds_config()
                                          ->mutable_api_config_source()
                                          ->add_config_validators();
      envoy::extensions::config::validators::dynamic_modules::v3::DynamicModuleConfigValidator
          config;
      config.mutable_dynamic_module_config()->set_name("config_validator_test");
      config.mutable_dynamic_module_config()->set_do_not_close(true);
      config.set_extension_name(extension_name);
      config.set_type_url(Config::TestTypeUrl::get().Cluster);

      Protobuf::StringValue extension_config;
      extension_config.set_value(extension_config_value);
      std::ignore = config.mutable_extension_config()->PackFrom(extension_config);

      std::ignore = config_validator_config->mutable_typed_config()->PackFrom(config);
      config_validator_config->set_name("envoy.config.validators.dynamic_modules");
    });
  }

  std::vector<envoy::config::cluster::v3::Cluster> clusters_;
  bool test_skipped_{true};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, DynamicModuleConfigValidatorIntegrationTest,
                         DELTA_SOTW_UNIFIED_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(DynamicModuleConfigValidatorIntegrationTest, RemoveRequiredClusterRejected) {
  addValidator("required_clusters", "cluster_0");
  initializeTest(2);

  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());

  std::vector<std::string> removed_clusters_names;
  std::transform(clusters_.cbegin(), clusters_.cend(), std::back_inserter(removed_clusters_names),
                 [](const envoy::config::cluster::v3::Cluster& cluster) -> std::string {
                   return cluster.name();
                 });

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "7", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TestTypeUrl::get().Cluster, {},
                                                             {}, {removed_clusters_names}, "8");

  const std::string expected_rejection =
      sotwOrDelta() == Grpc::SotwOrDelta::Sotw || sotwOrDelta() == Grpc::SotwOrDelta::UnifiedSotw
          ? "required cluster 'cluster_0' is absent"
          : "required cluster 'cluster_0' was removed";
  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "7", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Internal,
                                      expected_rejection));
  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());
}

TEST_P(DynamicModuleConfigValidatorIntegrationTest, RemoveNonRequiredClusterAccepted) {
  addValidator("required_clusters", "cluster_0");
  initializeTest(2);

  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());

  std::vector<envoy::config::cluster::v3::Cluster> remaining_clusters{clusters_[0]};

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "7", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, remaining_clusters, {}, {"cluster_1"}, "8");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "8", {}, {}, {}));
  EXPECT_EQ(2, test_server_->gauge("cluster_manager.active_clusters")->value());
}

// Exercises the validation context server-state callback. The min_active_clusters validator counts
// State-of-the-World resources directly and, on the delta path, reads the current dynamic cluster
// count to reject an update that would drop below the configured minimum. The rejection text pins
// the resulting count to one, so a broken dynamic cluster count callback would fail this test.
TEST_P(DynamicModuleConfigValidatorIntegrationTest, MinActiveClustersBelowThresholdRejected) {
  addValidator("min_active_clusters", "2");
  initializeTest(2);

  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());

  std::vector<envoy::config::cluster::v3::Cluster> remaining_clusters{clusters_[0]};

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "7", {}, {}, {}));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TestTypeUrl::get().Cluster, remaining_clusters, {}, {"cluster_1"}, "8");

  EXPECT_TRUE(compareDiscoveryRequest(Config::TestTypeUrl::get().Cluster, "7", {}, {}, {}, false,
                                      Grpc::Status::WellKnownGrpcStatus::Internal,
                                      "1 clusters, below the configured minimum of 2"));
  EXPECT_EQ(3, test_server_->gauge("cluster_manager.active_clusters")->value());
}

} // namespace
} // namespace Envoy
