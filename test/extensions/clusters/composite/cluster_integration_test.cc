#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/composite/v3/cluster.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class CompositeIntegrationTest : public HttpIntegrationTest {
public:
  CompositeIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void initialize() override {
    // Set up 3 upstream clusters for testing
    setUpstreamCount(3);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* primary_cluster = bootstrap.mutable_static_resources()->add_clusters();
      primary_cluster->set_name("primary_cluster");
      primary_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      primary_cluster->mutable_connect_timeout()->set_seconds(5);
      primary_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
      auto* primary_endpoint =
          primary_cluster->mutable_load_assignment()->add_endpoints()->add_lb_endpoints();
      primary_endpoint->mutable_endpoint()
          ->mutable_address()
          ->mutable_socket_address()
          ->set_address("127.0.0.1");
      primary_endpoint->mutable_endpoint()
          ->mutable_address()
          ->mutable_socket_address()
          ->set_port_value(0); // Will be updated

      auto* secondary_cluster = bootstrap.mutable_static_resources()->add_clusters();
      secondary_cluster->set_name("secondary_cluster");
      secondary_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      secondary_cluster->mutable_connect_timeout()->set_seconds(5);
      secondary_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
      auto* secondary_endpoint =
          secondary_cluster->mutable_load_assignment()->add_endpoints()->add_lb_endpoints();
      secondary_endpoint->mutable_endpoint()
          ->mutable_address()
          ->mutable_socket_address()
          ->set_address("127.0.0.1");
      secondary_endpoint->mutable_endpoint()
          ->mutable_address()
          ->mutable_socket_address()
          ->set_port_value(0); // Will be updated

      auto* tertiary_cluster = bootstrap.mutable_static_resources()->add_clusters();
      tertiary_cluster->set_name("tertiary_cluster");
      tertiary_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      tertiary_cluster->mutable_connect_timeout()->set_seconds(5);
      tertiary_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
      auto* tertiary_endpoint =
          tertiary_cluster->mutable_load_assignment()->add_endpoints()->add_lb_endpoints();
      tertiary_endpoint->mutable_endpoint()
          ->mutable_address()
          ->mutable_socket_address()
          ->set_address("127.0.0.1");
      tertiary_endpoint->mutable_endpoint()
          ->mutable_address()
          ->mutable_socket_address()
          ->set_port_value(0); // Will be updated

      // Composite cluster with USE_LAST_CLUSTER overflow (default)
      auto* composite_cluster = bootstrap.mutable_static_resources()->add_clusters();
      composite_cluster->set_name("composite_cluster");
      composite_cluster->mutable_connect_timeout()->set_seconds(5);
      composite_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
      auto* cluster_type = composite_cluster->mutable_cluster_type();
      cluster_type->set_name("envoy.clusters.composite");
      envoy::extensions::clusters::composite::v3::ClusterConfig composite_config;
      auto* primary_entry = composite_config.add_clusters();
      primary_entry->set_name("primary_cluster");
      auto* secondary_entry = composite_config.add_clusters();
      secondary_entry->set_name("secondary_cluster");
      composite_config.set_selection_strategy(
          envoy::extensions::clusters::composite::v3::ClusterConfig::SEQUENTIAL);
      composite_config.set_retry_overflow_option(
          envoy::extensions::clusters::composite::v3::ClusterConfig::USE_LAST_CLUSTER);
      cluster_type->mutable_typed_config()->PackFrom(composite_config);

      // Composite cluster with FAIL overflow
      auto* composite_fail_cluster = bootstrap.mutable_static_resources()->add_clusters();
      composite_fail_cluster->set_name("composite_fail_cluster");
      composite_fail_cluster->mutable_connect_timeout()->set_seconds(5);
      composite_fail_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
      auto* fail_cluster_type = composite_fail_cluster->mutable_cluster_type();
      fail_cluster_type->set_name("envoy.clusters.composite");
      envoy::extensions::clusters::composite::v3::ClusterConfig fail_config;
      auto* fail_primary = fail_config.add_clusters();
      fail_primary->set_name("primary_cluster");
      auto* fail_secondary = fail_config.add_clusters();
      fail_secondary->set_name("secondary_cluster");
      fail_config.set_selection_strategy(
          envoy::extensions::clusters::composite::v3::ClusterConfig::SEQUENTIAL);
      fail_config.set_retry_overflow_option(
          envoy::extensions::clusters::composite::v3::ClusterConfig::FAIL);
      fail_cluster_type->mutable_typed_config()->PackFrom(fail_config);

      // Composite cluster with ROUND_ROBIN overflow and three clusters
      auto* composite_rr_cluster = bootstrap.mutable_static_resources()->add_clusters();
      composite_rr_cluster->set_name("composite_rr_cluster");
      composite_rr_cluster->mutable_connect_timeout()->set_seconds(5);
      composite_rr_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
      auto* rr_cluster_type = composite_rr_cluster->mutable_cluster_type();
      rr_cluster_type->set_name("envoy.clusters.composite");
      envoy::extensions::clusters::composite::v3::ClusterConfig rr_config;
      auto* rr_primary = rr_config.add_clusters();
      rr_primary->set_name("primary_cluster");
      auto* rr_secondary = rr_config.add_clusters();
      rr_secondary->set_name("secondary_cluster");
      auto* rr_tertiary = rr_config.add_clusters();
      rr_tertiary->set_name("tertiary_cluster");
      rr_config.set_selection_strategy(
          envoy::extensions::clusters::composite::v3::ClusterConfig::SEQUENTIAL);
      rr_config.set_retry_overflow_option(
          envoy::extensions::clusters::composite::v3::ClusterConfig::ROUND_ROBIN);
      rr_cluster_type->mutable_typed_config()->PackFrom(rr_config);
    });

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* hcm_filter = filter_chain->mutable_filters(0);
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
      hcm_filter->mutable_typed_config()->UnpackTo(&hcm);

      auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
      virtual_host->clear_routes();

      // Direct cluster routes for testing
      auto* primary_route = virtual_host->add_routes();
      primary_route->mutable_match()->set_prefix("/primary");
      primary_route->mutable_route()->set_cluster("primary_cluster");

      auto* secondary_route = virtual_host->add_routes();
      secondary_route->mutable_match()->set_prefix("/secondary");
      secondary_route->mutable_route()->set_cluster("secondary_cluster");

      auto* tertiary_route = virtual_host->add_routes();
      tertiary_route->mutable_match()->set_prefix("/tertiary");
      tertiary_route->mutable_route()->set_cluster("tertiary_cluster");

      // Composite cluster route with retries
      auto* composite_route = virtual_host->add_routes();
      composite_route->mutable_match()->set_prefix("/composite");
      composite_route->mutable_route()->set_cluster("composite_cluster");
      auto* retry_policy = composite_route->mutable_route()->mutable_retry_policy();
      retry_policy->set_retry_on("5xx");
      retry_policy->mutable_num_retries()->set_value(3);

      // Composite FAIL cluster route
      auto* composite_fail_route = virtual_host->add_routes();
      composite_fail_route->mutable_match()->set_prefix("/composite_fail");
      composite_fail_route->mutable_route()->set_cluster("composite_fail_cluster");
      auto* fail_retry_policy = composite_fail_route->mutable_route()->mutable_retry_policy();
      fail_retry_policy->set_retry_on("5xx");
      fail_retry_policy->mutable_num_retries()->set_value(3);

      // Composite ROUND_ROBIN cluster route
      auto* composite_rr_route = virtual_host->add_routes();
      composite_rr_route->mutable_match()->set_prefix("/composite_rr");
      composite_rr_route->mutable_route()->set_cluster("composite_rr_cluster");
      auto* rr_retry_policy = composite_rr_route->mutable_route()->mutable_retry_policy();
      rr_retry_policy->set_retry_on("5xx");
      rr_retry_policy->mutable_num_retries()->set_value(4); // More retries to test round-robin

      // Limited retry scenario
      auto* limited_route = virtual_host->add_routes();
      limited_route->mutable_match()->set_prefix("/composite_limited");
      limited_route->mutable_route()->set_cluster("composite_cluster");
      auto* limited_retry_policy = limited_route->mutable_route()->mutable_retry_policy();
      limited_retry_policy->set_retry_on("5xx");
      limited_retry_policy->mutable_num_retries()->set_value(1); // Only 1 retry

      hcm_filter->mutable_typed_config()->PackFrom(hcm);
    });

    HttpIntegrationTest::initialize();
  }
};

// Test basic retry progression: Primary fails -> Secondary succeeds
TEST_F(CompositeIntegrationTest, BasicRetryProgression) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/composite"}, {":scheme", "http"}, {":authority", "host"}});

  // Primary cluster (fake_upstreams_[0]) should receive initial request
  waitForNextUpstreamRequest(0);
  // Make it fail with 500
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Secondary cluster (fake_upstreams_[1]) should receive retry
  waitForNextUpstreamRequest(1);
  // Make it succeed
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test USE_LAST_CLUSTER overflow: Primary fails, Secondary fails, Secondary used again for overflow
TEST_F(CompositeIntegrationTest, UseLastClusterOverflow) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/composite"}, {":scheme", "http"}, {":authority", "host"}});

  // Primary cluster fails
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Secondary cluster fails (first retry)
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Secondary cluster used again for overflow (second retry)
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Secondary cluster used again for overflow (third retry) - succeeds
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test FAIL overflow: Primary fails, Secondary fails, then give up
TEST_F(CompositeIntegrationTest, FailOverflow) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/composite_fail"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // Primary cluster fails
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Secondary cluster fails (first retry)
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // With FAIL overflow, no more retries should happen
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("500", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test ROUND_ROBIN overflow: Primary fails, Secondary fails, Tertiary fails, then round-robin back
TEST_F(CompositeIntegrationTest, RoundRobinOverflow) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/composite_rr"}, {":scheme", "http"}, {":authority", "host"}});

  // Primary cluster fails
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Secondary cluster fails (first retry)
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Tertiary cluster fails (second retry)
  waitForNextUpstreamRequest(2);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Round-robin back to primary cluster (third retry)
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Round-robin to secondary cluster (fourth retry) - succeeds
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test limited retry scenario
TEST_F(CompositeIntegrationTest, LimitedRetryScenario) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/composite_limited"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  // Primary cluster fails
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);

  // Secondary cluster should receive the single retry and succeed
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test direct cluster access still works
TEST_F(CompositeIntegrationTest, DirectClusterAccess) {
  initialize();

  // Test direct access to primary cluster
  testRouterHeaderOnlyRequestAndResponse(nullptr, 0, "/primary");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Test direct access to secondary cluster
  testRouterHeaderOnlyRequestAndResponse(nullptr, 1, "/secondary");
  cleanupUpstreamAndDownstream();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Test direct access to tertiary cluster
  testRouterHeaderOnlyRequestAndResponse(nullptr, 2, "/tertiary");
  cleanupUpstreamAndDownstream();
}

} // namespace Envoy
