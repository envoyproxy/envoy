#include "test/integration/filters/repick_cluster_filter.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class ShadowPolicyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  ShadowPolicyIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {
    setUpstreamCount(2);
  }

  void intitialConfigSetup(const std::string& cluster_name, const std::string& cluster_header) {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster->set_name(std::string(Envoy::RepickClusterFilter::ClusterName));
      ConfigHelper::setHttp2(*cluster);
      if (cluster_with_local_reply_filter_.has_value()) {
        auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(
            *cluster_with_local_reply_filter_);

        auto protocol_options = MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
            (*cluster->mutable_typed_extension_protocol_options())
                ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
        protocol_options.add_http_filters()->set_name("on-local-reply-filter");
        protocol_options.add_http_filters()->set_name("envoy.filters.http.upstream_codec");
        (*cluster->mutable_typed_extension_protocol_options())
            ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                .PackFrom(protocol_options);
      }
    });

    // Set the mirror policy with cluster header or cluster name.
    config_helper_.addConfigModifier(
        [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* mirror_policy = hcm.mutable_route_config()
                                    ->mutable_virtual_hosts(0)
                                    ->mutable_routes(0)
                                    ->mutable_route()
                                    ->add_request_mirror_policies();
          if (!cluster_header.empty()) {
            mirror_policy->set_cluster_header(cluster_header);
          } else {
            mirror_policy->set_cluster(cluster_name);
          }
        });
  }

  void sendRequestAndValidateResponse() {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test/long/url"},
                                                   {":scheme", "http"},
                                                   {":authority", "host"}};

    IntegrationStreamDecoderPtr response =
        sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0, 0);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(0U, response->body().size());

    cleanupUpstreamAndDownstream();
  }

  absl::optional<int> cluster_with_local_reply_filter_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ShadowPolicyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test request mirroring / shadowing with the cluster name in policy.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithCluster) {
  intitialConfigSetup("cluster_1", "");
  initialize();

  sendRequestAndValidateResponse();

  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
}

// Test request mirroring / shadowing with the original cluster having a local reply filter.
TEST_P(ShadowPolicyIntegrationTest, OriginalClusterWithLocalReply) {
  intitialConfigSetup("cluster_1", "");
  cluster_with_local_reply_filter_ = 0;
  setUpstreamProtocol(Http::CodecClient::Type::HTTP2);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());

  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 0);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
}

// Test request mirroring / shadowing with the mirror cluster having a local reply filter.
TEST_P(ShadowPolicyIntegrationTest, DISABLED_MirrorClusterWithLocalReply) {
  // TODO(alyssawilk) fix.
  intitialConfigSetup("cluster_1", "");
  cluster_with_local_reply_filter_ = 1;
  setUpstreamProtocol(Http::CodecClient::Type::HTTP2);
  initialize();

  sendRequestAndValidateResponse();

  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 0);
}

// Test request mirroring / shadowing with the cluster header.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithClusterHeaderWithFilter) {
  intitialConfigSetup("", "cluster_header_1");

  // Add a filter to set cluster_header in headers.
  config_helper_.addFilter("name: repick-cluster-filter");

  initialize();
  sendRequestAndValidateResponse();

  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
}

} // namespace
} // namespace Envoy
