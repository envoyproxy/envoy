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

class WeightedClusterIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  WeightedClusterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, Network::Address::IpVersion::v6) {
    default_weights_.reserve(upstream_count_);
    int total_cluster_with_name_count =
        upstream_count_ - Envoy::RepickClusterFilter::TotalUpstreamClusterWithHeaderCount;
    // For the simplicity of testing purpose, the default weighted cluster array
    // starts with weights for clusters with `name` filed and followed by
    // weights for clusters with `cluster_header` field.
    std::fill_n(std::back_inserter(default_weights_), total_cluster_with_name_count, 20);
    std::fill_n(std::back_inserter(default_weights_),
                Envoy::RepickClusterFilter::TotalUpstreamClusterWithHeaderCount, 30);
  }

  void createUpstreams() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    //  Add fake upstreams
    for (int i = 0; i < upstream_count_; ++i) {
      addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    }
  }

  void initializeConfig(const std::vector<uint64_t>& weights) {
    int total_cluster_with_name_count =
        upstream_count_ - Envoy::RepickClusterFilter::TotalUpstreamClusterWithHeaderCount;

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      for (int i = 1; i < upstream_count_; ++i) {
        auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
        cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        cluster->mutable_load_assignment()
            ->mutable_endpoints(0)
            ->mutable_lb_endpoints(0)
            ->mutable_endpoint()
            ->mutable_address()
            ->mutable_socket_address()
            ->set_address(fake_upstreams_[i]->localAddress()->ip()->addressAsString());
        cluster->set_name(absl::StrFormat(Envoy::RepickClusterFilter::ClusterName, i));
        ConfigHelper::setHttp2(*cluster);
      }

      auto* cluster_with_name = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster_with_name->set_name(absl::StrFormat(Envoy::RepickClusterFilter::ClusterName, 0));
      ConfigHelper::setHttp2(*cluster_with_name);

      config_helper_.addFilter("name: repick-cluster-filter");
    });

    // Modify route with weighted cluster configuration.
    config_helper_.addConfigModifier(
        [this, &total_cluster_with_name_count, &weights](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          for (int i = 0; i < total_cluster_with_name_count; ++i) {
            auto* cluster_with_name = hcm.mutable_route_config()
                                          ->mutable_virtual_hosts(0)
                                          ->mutable_routes(0)
                                          ->mutable_route()
                                          ->mutable_weighted_clusters()
                                          ->add_clusters();
            cluster_with_name->set_name(
                absl::StrFormat(Envoy::RepickClusterFilter::ClusterName, i));
            cluster_with_name->mutable_weight()->set_value(weights[i]);
          }

          for (int i = total_cluster_with_name_count; i < upstream_count_; ++i) {
            auto* cluster_with_header = hcm.mutable_route_config()
                                            ->mutable_virtual_hosts(0)
                                            ->mutable_routes(0)
                                            ->mutable_route()
                                            ->mutable_weighted_clusters()
                                            ->add_clusters();
            cluster_with_header->set_cluster_header(
                absl::StrFormat(Envoy::RepickClusterFilter::ClusterHeaderName, i));
            cluster_with_header->mutable_weight()->set_value(weights[i]);
          }

          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_route()
              ->mutable_weighted_clusters()
              ->mutable_total_weight()
              ->set_value(std::accumulate(weights.begin(), weights.end(), 0UL));
        });

    HttpIntegrationTest::initialize();
  }

  int getUpstreamCount() { return upstream_count_; }
  const std::vector<uint64_t>& getDefaultWeights() { return default_weights_; }

  void SendRequestAndValidateResponse(const std::vector<uint64_t>& upstream_indices) {
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
  int upstream_count_ = Envoy::RepickClusterFilter::TotalUpstreamClusterCount;
  std::vector<uint64_t> default_weights_;
};

TEST_F(WeightedClusterIntegrationTest, SteerTrafficToOneClusterWithName) {
  setDeterministicValue(0);

  initializeConfig(getDefaultWeights());

  // The expected destination cluster upstream is index 0 since the selected
  // value is set to 0 indirectly via `setDeterministicValue(0)`.
  SendRequestAndValidateResponse({0});
}

TEST_F(WeightedClusterIntegrationTest, SteerTrafficToOneClusterWithHeader) {
  const std::vector<uint64_t>& default_weights = getDefaultWeights();
  int total_cluster_with_name_count =
      getUpstreamCount() - Envoy::RepickClusterFilter::TotalUpstreamClusterWithHeaderCount;
  uint64_t deterministric_value = std::accumulate(
      default_weights.begin(), default_weights.begin() + total_cluster_with_name_count, 0UL);

  setDeterministicValue(deterministric_value);

  initializeConfig(default_weights);

  // The expected destination cluster upstream is index
  // `total_cluster_with_name_count`.
  SendRequestAndValidateResponse({static_cast<uint64_t>(total_cluster_with_name_count)});
}

TEST_F(WeightedClusterIntegrationTest, SplitTrafficRandomly) {
  std::vector<uint64_t> weights = {25, 25};
  initializeConfig(weights);

  std::vector<uint64_t> upstream_indices(getUpstreamCount());
  std::iota(std::begin(upstream_indices), std::end(upstream_indices), 0);
  for (int i = 0; i < 100; ++i) {
    // The expected destination cluster upstream is randomly selected based on
    // weight, so all the upstreams needs to be available for selection.
    SendRequestAndValidateResponse(upstream_indices);
  }

  // Check that all the upstream cluster have been routed to at least once.
  EXPECT_GE(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
  EXPECT_GE(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
}

} // namespace
} // namespace Envoy
