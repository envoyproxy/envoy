#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/mcp_multicluster/v3/cluster.pb.h"
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
    // Create 3 upstreams for the sub-clusters.
    setUpstreamCount(3);

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()->clear_clusters();

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

      // Add MCP multicluster.
      auto* multicluster = bootstrap.mutable_static_resources()->add_clusters();
      multicluster->set_name("multicluster");
      multicluster->mutable_connect_timeout()->set_seconds(5);
      multicluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
      multicluster->mutable_cluster_type()->set_name("envoy.clusters.mcp_multicluster");

      // Configure the MCP multicluster extension.
      envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig multicluster_config;
      for (int i = 0; i < 3; ++i) {
        auto* server = multicluster_config.add_servers();
        server->set_name(absl::StrCat("mcpserver_", i));
        server->mutable_mcp_cluster()->set_cluster(absl::StrCat("cluster_", i));
      }

      multicluster->mutable_cluster_type()->mutable_typed_config()->PackFrom(multicluster_config);
    });

    // Configure the route to use MCP multicluster.
    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          route->mutable_route()->set_cluster("multicluster");

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
    test_server_->waitForGauge("cluster_manager.active_clusters", testing::Ge(4));
  }

  void setNumRetries(uint32_t retries) { num_retries_ = retries; }

  void setEnableAttemptCountHeaders(bool enable) { enable_attempt_count_headers_ = enable; }

private:
  uint32_t num_retries_{3};
  bool enable_attempt_count_headers_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompositeClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// This is a placeholder that just validates functionality of the underlying composite cluster.
// TODO(yanvlasov): add tests with MCP router and MuxDemux async clients.
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

} // namespace Composite
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
