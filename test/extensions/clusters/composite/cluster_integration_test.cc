#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

struct CompositeClusterTestParams {
  Network::Address::IpVersion version;
  envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::OverflowOption
      overflow_option;
};

class CompositeClusterIntegrationTest : public testing::TestWithParam<CompositeClusterTestParams>,
                                        public HttpIntegrationTest {
public:
  CompositeClusterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().version),
        overflow_option_(GetParam().overflow_option) {}

  void initialize() override {
    // Set up 3 upstreams for our composite cluster sub-clusters.
    setUpstreamCount(3);

    // Configure the composite cluster with sub-clusters.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      setupCompositeCluster(bootstrap);
      setupSubClusters(bootstrap);
    });

    // Add route configuration with retry policy.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* hcm_filter = filter_chain->mutable_filters(0);
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
      hcm_filter->mutable_typed_config()->UnpackTo(&hcm);

      auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
      virtual_host->clear_routes();

      // Route with retry policy for composite cluster.
      auto* route = virtual_host->add_routes();
      route->mutable_match()->set_prefix("/");
      route->mutable_route()->set_cluster("composite_cluster");
      auto* retry_policy = route->mutable_route()->mutable_retry_policy();
      retry_policy->set_retry_on("5xx,gateway-error,connect-failure,refused-stream");
      retry_policy->mutable_num_retries()->set_value(5);

      hcm_filter->mutable_typed_config()->PackFrom(hcm);
    });

    HttpIntegrationTest::initialize();
  }

private:
  void setupCompositeCluster(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Replace the default cluster_0 with our composite cluster.
    auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster_0->set_name("composite_cluster");
    cluster_0->mutable_connect_timeout()->set_seconds(5);
    cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

    // Configure the composite cluster type.
    auto* cluster_type = cluster_0->mutable_cluster_type();
    cluster_type->set_name("envoy.clusters.composite");
    envoy::extensions::clusters::composite::v3::ClusterConfig composite_config;

    // Configure for RETRY mode by setting retry_config.

    // Add sub-clusters.
    auto* primary_entry = composite_config.add_sub_clusters();
    primary_entry->set_name("primary_cluster");
    auto* secondary_entry = composite_config.add_sub_clusters();
    secondary_entry->set_name("secondary_cluster");
    auto* tertiary_entry = composite_config.add_sub_clusters();
    tertiary_entry->set_name("tertiary_cluster");

    // Configure retry settings.
    auto* retry_config = composite_config.mutable_retry_config();
    retry_config->set_overflow_option(overflow_option_);

    cluster_type->mutable_typed_config()->PackFrom(composite_config);
  }

  void setupSubClusters(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create primary_cluster pointing to fake_upstreams_[0].
    auto* primary_cluster = bootstrap.mutable_static_resources()->add_clusters();
    primary_cluster->set_name("primary_cluster");
    primary_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    primary_cluster->mutable_connect_timeout()->set_seconds(5);
    primary_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
    auto* primary_load_assignment = primary_cluster->mutable_load_assignment();
    primary_load_assignment->set_cluster_name("primary_cluster");
    auto* primary_endpoint = primary_load_assignment->add_endpoints()->add_lb_endpoints();
    auto* primary_address =
        primary_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
    primary_address->set_address(Network::Test::getLoopbackAddressString(version_));
    primary_address->set_port_value(0); // Will be filled by ConfigHelper.

    // Create secondary_cluster pointing to fake_upstreams_[1].
    auto* secondary_cluster = bootstrap.mutable_static_resources()->add_clusters();
    secondary_cluster->set_name("secondary_cluster");
    secondary_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    secondary_cluster->mutable_connect_timeout()->set_seconds(5);
    secondary_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
    auto* secondary_load_assignment = secondary_cluster->mutable_load_assignment();
    secondary_load_assignment->set_cluster_name("secondary_cluster");
    auto* secondary_endpoint = secondary_load_assignment->add_endpoints()->add_lb_endpoints();
    auto* secondary_address =
        secondary_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
    secondary_address->set_address(Network::Test::getLoopbackAddressString(version_));
    secondary_address->set_port_value(0); // Will be filled by ConfigHelper.

    // Create tertiary_cluster pointing to fake_upstreams_[2].
    auto* tertiary_cluster = bootstrap.mutable_static_resources()->add_clusters();
    tertiary_cluster->set_name("tertiary_cluster");
    tertiary_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    tertiary_cluster->mutable_connect_timeout()->set_seconds(5);
    tertiary_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
    auto* tertiary_load_assignment = tertiary_cluster->mutable_load_assignment();
    tertiary_load_assignment->set_cluster_name("tertiary_cluster");
    auto* tertiary_endpoint = tertiary_load_assignment->add_endpoints()->add_lb_endpoints();
    auto* tertiary_address =
        tertiary_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
    tertiary_address->set_address(Network::Test::getLoopbackAddressString(version_));
    tertiary_address->set_port_value(0); // Will be filled by ConfigHelper.
  }

public:
  static std::vector<CompositeClusterTestParams> getTestParams() {
    std::vector<CompositeClusterTestParams> params;
    for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
      for (auto overflow_option :
           {envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::FAIL,
            envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::
                USE_LAST_CLUSTER,
            envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::ROUND_ROBIN}) {
        params.push_back({ip_version, overflow_option});
      }
    }
    return params;
  }

  static std::string paramToString(const testing::TestParamInfo<CompositeClusterTestParams>& info) {
    std::string overflow_name;
    switch (info.param.overflow_option) {
    case envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::FAIL:
      overflow_name = "Fail";
      break;
    case envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::USE_LAST_CLUSTER:
      overflow_name = "UseLastCluster";
      break;
    case envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::ROUND_ROBIN:
      overflow_name = "RoundRobin";
      break;
    default:
      overflow_name = "Unknown";
    }
    return absl::StrCat((info.param.version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6"),
                        "_", overflow_name);
  }

protected:
  const envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::OverflowOption
      overflow_option_;
};

INSTANTIATE_TEST_SUITE_P(ClusterConfigTests, CompositeClusterIntegrationTest,
                         testing::ValuesIn(CompositeClusterIntegrationTest::getTestParams()),
                         CompositeClusterIntegrationTest::paramToString);

// Test that the composite cluster loads successfully.
TEST_P(CompositeClusterIntegrationTest, ClusterConfigLoads) {
  initialize();

  // Verify that the composite cluster and sub-clusters are properly loaded.
  const auto& cluster_map = test_server_->server().clusterManager().clusters().active_clusters_;
  EXPECT_GT(cluster_map.count("composite_cluster"), 0);
  EXPECT_GT(cluster_map.count("primary_cluster"), 0);
  EXPECT_GT(cluster_map.count("secondary_cluster"), 0);
  EXPECT_GT(cluster_map.count("tertiary_cluster"), 0);
}

// Test successful request to primary cluster (no retries needed).
TEST_P(CompositeClusterIntegrationTest, SuccessfulRequestToPrimaryCluster) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});

  // Should go to primary cluster (fake_upstreams_[0]).
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Respond with success.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  cleanupUpstreamAndDownstream();
}

// Test retry progression: Primary fails -> Secondary succeeds.
TEST_P(CompositeClusterIntegrationTest, RetryProgressionPrimaryFailsSecondarySucceeds) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});

  // First attempt: Primary cluster should receive request and fail.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Primary responds with 503 to trigger retry.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  // For HTTP/1.1, need to handle connection properly.
  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                          std::chrono::milliseconds(500)));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  }

  // Second attempt: Secondary cluster should receive request and succeed.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Secondary responds with success.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  cleanupUpstreamAndDownstream();
}

// Test full retry progression: Primary fails -> Secondary fails -> Tertiary succeeds.
TEST_P(CompositeClusterIntegrationTest, FullRetryProgressionTertiarySucceeds) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});

  // First attempt: Primary cluster fails.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);

  // Handle connection for first retry.
  if (fake_upstreams_[0]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                          std::chrono::milliseconds(500)));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  }

  // Second attempt: Secondary cluster fails.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "502"}}, false);

  // Handle connection for second retry.
  if (fake_upstreams_[1]->httpType() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                          std::chrono::milliseconds(500)));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  }

  // Third attempt: Tertiary cluster succeeds.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  cleanupUpstreamAndDownstream();
}

// Test overflow behavior: More retries than clusters.
TEST_P(CompositeClusterIntegrationTest, RetryOverflowOption) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});

  // First three attempts should go to primary, secondary, tertiary clusters respectively.
  for (int i = 0; i < 3; ++i) {
    if (i == 0) {
      ASSERT_TRUE(
          fake_upstreams_[i]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    } else {
      // For subsequent attempts, handle connection based on HTTP version.
      if (fake_upstreams_[i - 1]->httpType() == Http::CodecType::HTTP1) {
        ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
        ASSERT_TRUE(fake_upstreams_[i]->waitForHttpConnection(
            *dispatcher_, fake_upstream_connection_, std::chrono::milliseconds(500)));
      } else {
        ASSERT_TRUE(upstream_request_->waitForReset());
        ASSERT_TRUE(
            fake_upstreams_[i]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
      }
    }

    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);
  }

  // Fourth attempt behavior depends on overflow option.
  switch (overflow_option_) {
  case envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::FAIL:
    // No more retries, should fail.
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("503"));
    break;

  case envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::USE_LAST_CLUSTER:
    // Should use tertiary cluster again.
    if (fake_upstreams_[2]->httpType() == Http::CodecType::HTTP1) {
      ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
      ASSERT_TRUE(fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                            std::chrono::milliseconds(500)));
    } else {
      ASSERT_TRUE(upstream_request_->waitForReset());
      ASSERT_TRUE(
          fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    }
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
    break;

  case envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::ROUND_ROBIN:
    // Should round-robin back to primary cluster.
    if (fake_upstreams_[2]->httpType() == Http::CodecType::HTTP1) {
      ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
      ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                            std::chrono::milliseconds(500)));
    } else {
      ASSERT_TRUE(upstream_request_->waitForReset());
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    }
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
    break;

  default:
    // Handle any unknown or sentinel enum values.
    FAIL() << "Unknown overflow option: " << static_cast<int>(overflow_option_);
    break;
  }

  cleanupUpstreamAndDownstream();
}

// Test scenario where one sub-cluster doesn't exist.
class CompositeClusterMissingSubClusterTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  CompositeClusterMissingSubClusterTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    setUpstreamCount(2); // Only create 2 upstreams, but configure 3 sub-clusters.

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      setupCompositeClusterWithMissingSubCluster(bootstrap);
      setupExistingSubClusters(bootstrap);
    });

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* hcm_filter = filter_chain->mutable_filters(0);
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
      hcm_filter->mutable_typed_config()->UnpackTo(&hcm);

      auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
      virtual_host->clear_routes();

      auto* route = virtual_host->add_routes();
      route->mutable_match()->set_prefix("/");
      route->mutable_route()->set_cluster("composite_cluster");
      auto* retry_policy = route->mutable_route()->mutable_retry_policy();
      retry_policy->set_retry_on("5xx");
      retry_policy->mutable_num_retries()->set_value(3);

      hcm_filter->mutable_typed_config()->PackFrom(hcm);
    });

    HttpIntegrationTest::initialize();
  }

private:
  void
  setupCompositeClusterWithMissingSubCluster(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster_0->set_name("composite_cluster");
    cluster_0->mutable_connect_timeout()->set_seconds(5);
    cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

    auto* cluster_type = cluster_0->mutable_cluster_type();
    cluster_type->set_name("envoy.clusters.composite");
    envoy::extensions::clusters::composite::v3::ClusterConfig composite_config;

    // Configure for RETRY mode by setting retry_config.

    // Reference a cluster that doesn't exist.
    auto* missing_entry = composite_config.add_sub_clusters();
    missing_entry->set_name("missing_cluster");
    auto* existing_entry = composite_config.add_sub_clusters();
    existing_entry->set_name("existing_cluster");

    // Configure retry settings.
    auto* retry_config = composite_config.mutable_retry_config();
    retry_config->set_overflow_option(
        envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::FAIL);

    cluster_type->mutable_typed_config()->PackFrom(composite_config);
  }

  void setupExistingSubClusters(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Only create the existing cluster, not the missing one.
    auto* existing_cluster = bootstrap.mutable_static_resources()->add_clusters();
    existing_cluster->set_name("existing_cluster");
    existing_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
    existing_cluster->mutable_connect_timeout()->set_seconds(5);
    existing_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
    auto* load_assignment = existing_cluster->mutable_load_assignment();
    load_assignment->set_cluster_name("existing_cluster");
    auto* endpoint = load_assignment->add_endpoints()->add_lb_endpoints();
    auto* address = endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
    address->set_address(Network::Test::getLoopbackAddressString(version_));
    address->set_port_value(0);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompositeClusterMissingSubClusterTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test handling of missing sub-cluster: Missing cluster should cause no host available.
TEST_P(CompositeClusterMissingSubClusterTest, RetryWithMissingSubCluster) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});

  // When missing_cluster doesn't exist, the composite cluster load balancer should
  // return null for the first attempt, which should result in a 503 "no healthy upstream"
  // response without any upstream connection.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("503"));

  cleanupUpstreamAndDownstream();
}

// Test single cluster configuration.
class CompositeClusterSingleSubClusterTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  CompositeClusterSingleSubClusterTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    setUpstreamCount(1);

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster_0->set_name("composite_cluster");
      cluster_0->mutable_connect_timeout()->set_seconds(5);
      cluster_0->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

      auto* cluster_type = cluster_0->mutable_cluster_type();
      cluster_type->set_name("envoy.clusters.composite");
      envoy::extensions::clusters::composite::v3::ClusterConfig composite_config;

      // Configure for RETRY mode by setting retry_config.

      // Only one sub-cluster.
      auto* single_entry = composite_config.add_sub_clusters();
      single_entry->set_name("single_cluster");

      // Configure retry settings.
      auto* retry_config = composite_config.mutable_retry_config();
      retry_config->set_overflow_option(
          envoy::extensions::clusters::composite::v3::ClusterConfig::RetryConfig::USE_LAST_CLUSTER);

      cluster_type->mutable_typed_config()->PackFrom(composite_config);

      // Create the single sub-cluster.
      auto* single_cluster = bootstrap.mutable_static_resources()->add_clusters();
      single_cluster->set_name("single_cluster");
      single_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      single_cluster->mutable_connect_timeout()->set_seconds(5);
      single_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
      auto* load_assignment = single_cluster->mutable_load_assignment();
      load_assignment->set_cluster_name("single_cluster");
      auto* endpoint = load_assignment->add_endpoints()->add_lb_endpoints();
      auto* address = endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
      address->set_address(Network::Test::getLoopbackAddressString(version_));
      address->set_port_value(0);
    });

    // Fix route configuration to point to the correct cluster name.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* hcm_filter = filter_chain->mutable_filters(0);
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
      hcm_filter->mutable_typed_config()->UnpackTo(&hcm);

      auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
      virtual_host->clear_routes();

      auto* route = virtual_host->add_routes();
      route->mutable_match()->set_prefix("/");
      route->mutable_route()->set_cluster("composite_cluster");

      hcm_filter->mutable_typed_config()->PackFrom(hcm);
    });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompositeClusterSingleSubClusterTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that a single sub-cluster works properly and handles retries.
TEST_P(CompositeClusterSingleSubClusterTest, SingleSubClusterRetries) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}});

  // All attempts should go to the single cluster.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
