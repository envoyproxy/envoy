#include <cstdint>
#include <iterator>
#include <numeric>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/filters/repick_cluster_filter.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class WeightedClusterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public HttpIntegrationTest {
public:
  WeightedClusterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void createUpstreams() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    //  Add two fake upstreams
    for (int i = 0; i < 2; ++i) {
      addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    }
  }

  void initializeConfig(const std::vector<uint64_t>& weights) {
    // Set the cluster configuration for `cluster_1`
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster->set_name(std::string(Envoy::RepickClusterFilter::ClusterName));
      ConfigHelper::setHttp2(*cluster);
    });

    // Add the custom filter.
    config_helper_.addFilter("name: repick-cluster-filter");

    // Modify route with weighted cluster configuration.
    config_helper_.addConfigModifier(
        [&weights](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* weighted_clusters = hcm.mutable_route_config()
                                        ->mutable_virtual_hosts(0)
                                        ->mutable_routes(0)
                                        ->mutable_route()
                                        ->mutable_weighted_clusters();

          // Add a cluster with `name` specified.
          auto* cluster = weighted_clusters->add_clusters();
          cluster->set_name("cluster_0");
          cluster->mutable_weight()->set_value(weights[0]);

          // Add a cluster with `cluster_header` specified.
          cluster = weighted_clusters->add_clusters();
          cluster->set_cluster_header(std::string(Envoy::RepickClusterFilter::ClusterHeaderName));
          cluster->mutable_weight()->set_value(weights[1]);
        });

    HttpIntegrationTest::initialize();
  }

  const std::vector<uint64_t>& getDefaultWeights() { return default_weights_; }

  void sendRequestAndValidateResponse(const std::vector<uint64_t>& upstream_indices) {
    // Create a client aimed at Envoy’s default HTTP port.
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

    // Create some request headers.
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test/long/url"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"}};

    // Send the request headers from the client, wait until they are received
    // upstream. When they are received, send the default response headers from
    // upstream and wait until they are received at by client.
    IntegrationStreamDecoderPtr response = sendRequestAndWaitForResponse(
        request_headers, 0, default_response_headers_, 0, upstream_indices);

    // Verify the proxied request was received upstream, as expected.
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());
    // Verify the proxied response was received downstream, as expected.
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(0U, response->body().size());

    // Perform the clean-up.
    cleanupUpstreamAndDownstream();
  }

private:
  std::vector<uint64_t> default_weights_ = {20, 30};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, WeightedClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Steer the traffic (i.e. send the request) to the weighted cluster with `name` specified.
TEST_P(WeightedClusterIntegrationTest, SteerTrafficToOneClusterWithName) {
  setDeterministicValue();
  initializeConfig(getDefaultWeights());

  // The expected destination cluster upstream is index 0 since the selected
  // value is set to 0 indirectly via `setDeterministicValue()` above to set the weight to 0.
  sendRequestAndValidateResponse({0});

  // Check that the expected upstream cluster has incoming request.
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
}

// Steer the traffic (i.e. send the request) to the weighted cluster with `cluster_header`
// specified.
TEST_P(WeightedClusterIntegrationTest, SteerTrafficToOneClusterWithHeader) {
  const std::vector<uint64_t>& default_weights = getDefaultWeights();

  // The index of the cluster with `cluster_header` specified is 1.
  int cluster_header_index = 1;
  // Set the deterministic value to the accumulation of the weights of all clusters with
  // `name`, so we can route the traffic to the first cluster with `cluster_header` based on
  // weighted cluster selection algorithm in `RouteEntryImplBase::pickWeightedCluster()`.
  uint64_t deterministric_value =
      std::accumulate(default_weights.begin(), default_weights.begin() + cluster_header_index, 0UL);
  setDeterministicValue(deterministric_value);

  initializeConfig(default_weights);

  sendRequestAndValidateResponse({static_cast<uint64_t>(cluster_header_index)});

  // Check that the expected upstream cluster has incoming request.
  std::string target_name =
      absl::StrFormat("cluster.cluster_%d.upstream_cx_total", cluster_header_index);
  EXPECT_EQ(test_server_->counter(target_name)->value(), 1);
}

// Steer the traffic (i.e. send the request) to the weighted clusters randomly based on weight.
TEST_P(WeightedClusterIntegrationTest, SplitTrafficRandomly) {
  std::vector<uint64_t> weights = {50, 50};
  int upstream_count = weights.size();
  initializeConfig(weights);

  std::vector<uint64_t> upstream_indices(upstream_count);
  std::iota(std::begin(upstream_indices), std::end(upstream_indices), 0);
  int request_num = 20;
  for (int i = 0; i < request_num; ++i) {
    // The expected destination cluster upstream is randomly selected based on
    // weight, so all the upstreams needs to be available for selection.
    sendRequestAndValidateResponse(upstream_indices);
  }

  std::string target_name;
  // Check that all the upstream clusters have been routed to at least once.
  for (int i = 0; i < upstream_count; ++i) {
    target_name = absl::StrFormat("cluster.cluster_%d.upstream_cx_total", i);
    EXPECT_GE(test_server_->counter(target_name)->value(), 1);
  }
}

} // namespace
} // namespace Envoy
