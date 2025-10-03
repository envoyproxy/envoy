#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Composite {

class CompositeClusterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  CompositeClusterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    // We need 3 upstreams for the sub-clusters.
    setUpstreamCount(3);

    // Modify the bootstrap config to set up our composite cluster.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Clear existing clusters.
      bootstrap.mutable_static_resources()->clear_clusters();

      // Add 3 regular static clusters.
      for (int i = 0; i < 3; ++i) {
        auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
        cluster->set_name(absl::StrCat("cluster_", i));
        cluster->mutable_connect_timeout()->set_seconds(5);
        cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
        cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);

        auto* load_assignment = cluster->mutable_load_assignment();
        load_assignment->set_cluster_name(cluster->name());
        auto* endpoints = load_assignment->add_endpoints();
        auto* lb_endpoint = endpoints->add_lb_endpoints();
        auto* endpoint = lb_endpoint->mutable_endpoint();
        auto* address = endpoint->mutable_address()->mutable_socket_address();
        address->set_address(Network::Test::getLoopbackAddressString(GetParam()));
        address->set_port_value(fake_upstreams_[i]->localAddress()->ip()->port());
      }

      // Add the composite cluster.
      auto* composite_cluster = bootstrap.mutable_static_resources()->add_clusters();
      composite_cluster->set_name("composite_cluster");
      composite_cluster->mutable_connect_timeout()->set_seconds(5);
      composite_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

      // Set up the cluster type.
      composite_cluster->mutable_cluster_type()->set_name("envoy.clusters.composite");

      // Configure the composite extension.
      envoy::extensions::clusters::composite::v3::ClusterConfig composite_config;
      composite_config.add_clusters("cluster_0");
      composite_config.add_clusters("cluster_1");
      composite_config.add_clusters("cluster_2");
      composite_config.set_overflow_option(overflow_behavior_);

      composite_cluster->mutable_cluster_type()->mutable_typed_config()->PackFrom(composite_config);
    });

    // Configure the route to use our composite cluster.
    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          route->mutable_route()->set_cluster("composite_cluster");

          // Configure retry policy.
          auto* retry_policy = route->mutable_route()->mutable_retry_policy();
          retry_policy->set_retry_on("5xx");
          retry_policy->mutable_num_retries()->set_value(num_retries_);

          if (enable_attempt_count_headers_) {
            auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
            virtual_host->set_include_request_attempt_count(true);
            virtual_host->set_include_attempt_count_in_response(true);
          }
        });

    HttpIntegrationTest::initialize();

    // Verify clusters are created.
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 4);
  }

  void setOverflowBehavior(
      envoy::extensions::clusters::composite::v3::ClusterConfig::OverflowOption behavior) {
    overflow_behavior_ = behavior;
  }

  void setNumRetries(uint32_t retries) { num_retries_ = retries; }

  void setEnableAttemptCountHeaders(bool enable) { enable_attempt_count_headers_ = enable; }

private:
  envoy::extensions::clusters::composite::v3::ClusterConfig::OverflowOption overflow_behavior_{
      envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL};
  uint32_t num_retries_{3};
  bool enable_attempt_count_headers_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompositeClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test 1: Basic Retry Progression.
// Verifies that retries progress through clusters in order: cluster_0 -> cluster_1 -> cluster_2.
TEST_P(CompositeClusterIntegrationTest, BasicRetryProgression) {
  setNumRetries(3);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Create a request.
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test"},
                                     {":scheme", "http"},
                                     {":authority", "test.example.com"}},
      0);

  // First attempt should go to cluster_0 - return 503 to trigger retry.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  fake_upstream_connection_.reset();

  // First retry should go to cluster_1 - return 503 to trigger another retry.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  fake_upstream_connection_.reset();

  // Second retry should go to cluster_2 - return 200.
  ASSERT_TRUE(fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify each cluster was used exactly once.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
}

// Test 2: Successful First Attempt (No Retries).
// Verifies that successful requests don't trigger retries.
TEST_P(CompositeClusterIntegrationTest, SuccessfulFirstAttempt) {
  setNumRetries(3);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test"},
                                     {":scheme", "http"},
                                     {":authority", "test.example.com"}},
      0);

  // First attempt should go to cluster_0 - return 200.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify only cluster_0 was used.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
}

// Test 3: FAIL Overflow Behavior.
// Verifies that requests fail when retries exceed available clusters with FAIL behavior.
TEST_P(CompositeClusterIntegrationTest, FailOverflowBehavior) {
  setOverflowBehavior(envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);
  setNumRetries(5); // More retries than clusters.
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test"},
                                     {":scheme", "http"},
                                     {":authority", "test.example.com"}},
      0);

  // All three clusters return 503.
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(fake_upstreams_[i]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
    ASSERT_TRUE(fake_upstream_connection_->close());
    fake_upstream_connection_.reset();
  }

  // No more clusters available - request should fail with 503.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  // Verify each cluster was attempted once.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
}

// Test 4: USE_LAST_CLUSTER Overflow Behavior.
// Verifies that overflow retries continue to use the last cluster.
TEST_P(CompositeClusterIntegrationTest, UseLastClusterOverflowBehavior) {
  setOverflowBehavior(envoy::extensions::clusters::composite::v3::ClusterConfig::USE_LAST_CLUSTER);
  setNumRetries(5); // More retries than clusters.
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test"},
                                     {":scheme", "http"},
                                     {":authority", "test.example.com"}},
      0);

  // First 3 attempts go to different clusters.
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(fake_upstreams_[i]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
    ASSERT_TRUE(fake_upstream_connection_->close());
    fake_upstream_connection_.reset();
  }

  // 4th attempt should go to cluster_2 again (last cluster).
  ASSERT_TRUE(fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  fake_upstream_connection_.reset();

  // 5th attempt should go to cluster_2 again - return 200.
  ASSERT_TRUE(fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify cluster usage - cluster_2 should have been used 3 times.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(3, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
}

// Test 5: ROUND_ROBIN Overflow Behavior.
// Verifies that overflow retries round-robin through available clusters.
TEST_P(CompositeClusterIntegrationTest, RoundRobinOverflowBehavior) {
  setOverflowBehavior(envoy::extensions::clusters::composite::v3::ClusterConfig::ROUND_ROBIN);
  setNumRetries(5); // More retries than clusters.
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test"},
                                     {":scheme", "http"},
                                     {":authority", "test.example.com"}},
      0);

  // First 3 attempts go to different clusters.
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(fake_upstreams_[i]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
    ASSERT_TRUE(fake_upstream_connection_->close());
    fake_upstream_connection_.reset();
  }

  // 4th attempt should round-robin back to cluster_0.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  fake_upstream_connection_.reset();

  // 5th attempt should round-robin to cluster_1 - return 200.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify cluster usage - cluster_0 and cluster_1 used twice, cluster_2 once.
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
}

// Test 6: Retry Attempt Count Verification.
// This test specifically verifies the 1-based retry attempt indexing.
TEST_P(CompositeClusterIntegrationTest, RetryAttemptCountVerification) {
  setNumRetries(2);
  setEnableAttemptCountHeaders(true);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test"},
                                     {":scheme", "http"},
                                     {":authority", "test.example.com"}},
      0);

  // First attempt (attempt count = 1) should go to cluster_0.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_EQ("1", upstream_request_->headers().getEnvoyAttemptCountValue());
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  fake_upstream_connection_.reset();

  // First retry (attempt count = 2) should go to cluster_1.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_EQ("2", upstream_request_->headers().getEnvoyAttemptCountValue());
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("2", response->headers().getEnvoyAttemptCountValue());

  // Verify correct cluster usage.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
}

// Test 7: No Retries Configuration.
// Verifies behavior when no retries are configured.
TEST_P(CompositeClusterIntegrationTest, NoRetriesConfigured) {
  setNumRetries(0); // No retries allowed.
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test"},
                                     {":scheme", "http"},
                                     {":authority", "test.example.com"}},
      0);

  // First and only attempt should go to cluster_0.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  // Verify only cluster_0 was used.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
}

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
