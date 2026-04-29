#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

// Parameterized over (language, IP version). language selects which test_data subdir is
// loaded as the dynamic module — currently rust or go. Each language ships a module named
// "cluster_integration_test" that exposes the same set of named cluster types
// (sync_host_selection, async_host_selection, scheduler_host_update, lifecycle_callbacks),
// so the same test bodies exercise both SDKs.
struct ClusterIntegrationParam {
  std::string language;
  Network::Address::IpVersion ip_version;
};

class DynamicModuleClusterIntegrationTest
    : public testing::TestWithParam<ClusterIntegrationParam>,
      public HttpIntegrationTest {
public:
  DynamicModuleClusterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().ip_version) {}

  void initializeWithDecCluster(const std::string& cluster_name,
                                const std::string& cluster_config = "") {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/" +
                                      GetParam().language),
        1);

    // Replace the default cluster_0 with a DEC cluster that uses the language module
    // selected by the test parameter.
    config_helper_.addConfigModifier([this, cluster_name, cluster_config](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

      // Use asString() which correctly formats IPv4 (127.0.0.1:port) and
      // IPv6 ([::1]:port) for parseInternetAddressAndPortNoThrow.
      const std::string upstream_address = fake_upstreams_[0]->localAddress()->asString();

      // Configure the cluster as a DEC cluster.
      cluster->set_name("cluster_0");
      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
      cluster->clear_load_assignment();

      envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig dec_config;
      dec_config.mutable_dynamic_module_config()->set_name("cluster_integration_test");
      dec_config.set_cluster_name(cluster_name);

      // Pass the upstream address via the cluster config so the module knows where to add
      // hosts.
      const std::string config_value = cluster_config.empty() ? upstream_address : cluster_config;
      Protobuf::StringValue config_proto;
      config_proto.set_value(config_value);
      dec_config.mutable_cluster_config()->PackFrom(config_proto);

      cluster->mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
      cluster->mutable_cluster_type()->mutable_typed_config()->PackFrom(dec_config);
    });

    HttpIntegrationTest::initialize();
  }
};

namespace {
std::vector<ClusterIntegrationParam> getClusterIntegrationTestParams() {
  std::vector<ClusterIntegrationParam> params;
  for (const auto& language : {"rust", "go"}) {
    for (const auto ip : TestEnvironment::getIpVersionsForTest()) {
      params.push_back({language, ip});
    }
  }
  return params;
}

std::string clusterIntegrationParamName(
    const testing::TestParamInfo<ClusterIntegrationParam>& info) {
  return info.param.language + "_" +
         (info.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6");
}
} // namespace

INSTANTIATE_TEST_SUITE_P(SdkLanguagesAndIpVersions, DynamicModuleClusterIntegrationTest,
                         testing::ValuesIn(getClusterIntegrationTestParams()),
                         clusterIntegrationParamName);

// Verifies that a cluster with synchronous host selection correctly routes requests
// to the upstream added during on_init.
TEST_P(DynamicModuleClusterIntegrationTest, SyncHostSelection) {
  initializeWithDecCluster("sync_host_selection");
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verifies that multiple requests through a synchronous cluster all succeed,
// exercising the round-robin host selection path.
TEST_P(DynamicModuleClusterIntegrationTest, SyncHostSelectionMultipleRequests) {
  initializeWithDecCluster("sync_host_selection");
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  for (int i = 0; i < 3; ++i) {
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

// Verifies that a cluster with asynchronous host selection correctly routes requests.
//
// For Go specifically, this exercises the bug fix where ChooseHost was unable to honor a
// user-supplied ClusterAsyncHostSelection — the SDK previously discarded the user's
// returned handle and registered a fresh one, making Cancel dispatch unreachable.
TEST_P(DynamicModuleClusterIntegrationTest, AsyncHostSelection) {
  initializeWithDecCluster("async_host_selection");
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verifies that a cluster can use the scheduler to add hosts after initialization.
TEST_P(DynamicModuleClusterIntegrationTest, SchedulerHostUpdate) {
  initializeWithDecCluster("scheduler_host_update");
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verifies that the cluster lifecycle callbacks fire correctly during cluster
// initialization, and — critically for Go — that the shutdown completion callback
// reaches Envoy. A previous bug at the trampoline layer silently dropped the completion,
// causing test_server_.reset() at the end of the test to hang waiting for an event_cb
// that never arrived.
TEST_P(DynamicModuleClusterIntegrationTest, LifecycleCallbacks) {
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({{"info", "cluster lifecycle: on_init called"},
                                  {"info", "cluster lifecycle: on_server_initialized called"}}),
      initializeWithDecCluster("lifecycle_callbacks"));

  // Send a request to verify the cluster is functional after lifecycle callbacks.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Tear the server down explicitly so the on_shutdown hook fires inside this test scope
  // — that lets us assert the completion callback reaches Envoy. If the bug regresses on
  // Go, the reset hangs and the test times out.
  EXPECT_LOG_CONTAINS("info", "cluster lifecycle: on_shutdown called",
                      { test_server_.reset(); });
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
