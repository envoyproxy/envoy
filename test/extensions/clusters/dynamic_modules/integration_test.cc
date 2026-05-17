#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

namespace {

// ObjectFactory used by the cluster filter-state read test: the dynamic-module HTTP filter
// writes through this factory via envoy_dynamic_module_callback_http_set_filter_state_typed,
// and the dynamic-module cluster reads it back during host selection via
// envoy_dynamic_module_callback_cluster_lb_context_get_filter_state_typed.
class ClusterTypedObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "envoy.test.cluster_typed_object"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(ClusterTypedObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace

class DynamicModuleClusterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleClusterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithDecCluster(const std::string& cluster_name,
                                const std::string& cluster_config = "") {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/rust"), 1);

    // Replace the default cluster_0 with a DEC cluster that uses the Rust module.
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

      // Pass the upstream address via the cluster config so the Rust module knows
      // where to add hosts.
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

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

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
// initialization.
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
}

// =============================================================================
// Filter-state read ABI: an upstream HTTP filter writes filter state on the
// request path; the dynamic-module cluster reads it back during host selection.
// =============================================================================
class DynamicModuleClusterFilterStateIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleClusterFilterStateIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithProducerAndReader() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/rust"), 1);

    // Prepend the dynamic-module HTTP filter so it writes filter state on every
    // request before the router runs.
    constexpr absl::string_view producer_filter_config = R"EOF(
name: envoy.extensions.filters.http.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
  dynamic_module_config:
    name: cluster_filter_state_test
  filter_name: filter_state_producer
  filter_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: ""
)EOF";
    config_helper_.prependFilter(std::string(producer_filter_config));

    // Replace cluster_0 with a dynamic-module cluster whose Rust load balancer
    // reads the filter state we just wrote.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      const std::string upstream_address = fake_upstreams_[0]->localAddress()->asString();

      cluster->set_name("cluster_0");
      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
      cluster->clear_load_assignment();

      envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig reader_config;
      reader_config.mutable_dynamic_module_config()->set_name("cluster_filter_state_test");
      reader_config.set_cluster_name("filter_state_reader");

      Protobuf::StringValue config_proto;
      config_proto.set_value(upstream_address);
      reader_config.mutable_cluster_config()->PackFrom(config_proto);

      cluster->mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
      cluster->mutable_cluster_type()->mutable_typed_config()->PackFrom(reader_config);
    });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleClusterFilterStateIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that filter state written by an upstream HTTP filter on the request
// path is observable in the dynamic-module cluster's choose_host callback via
// both the bytes and the typed filter-state ABI accessors. The cluster returns
// its only host only when both values match the producer's payload, so a 200
// response proves the round trip.
TEST_P(DynamicModuleClusterFilterStateIntegrationTest, ReadsFilterStateProducedByHttpFilter) {
  initializeWithProducerAndReader();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
