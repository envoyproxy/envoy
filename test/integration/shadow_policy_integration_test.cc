#include <string>

#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/filters/repick_cluster_filter.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class ShadowPolicyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  ShadowPolicyIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    autonomous_upstream_ = true;
    setUpstreamCount(2);
  }

  // Adds a mirror policy that routes to cluster_header or cluster_name, in that order. Additionally
  // optionally registers an upstream filter on the cluster specified by
  // cluster_with_custom_filter_.
  void initialConfigSetup(const std::string& cluster_name, const std::string& cluster_header) {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster->set_name(std::string(Envoy::RepickClusterFilter::ClusterName));
      ConfigHelper::setHttp2(*cluster);
      if (cluster_with_custom_filter_.has_value()) {
        auto* cluster =
            bootstrap.mutable_static_resources()->mutable_clusters(*cluster_with_custom_filter_);

        ConfigHelper::HttpProtocolOptions protocol_options =
            MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
                (*cluster->mutable_typed_extension_protocol_options())
                    ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
        protocol_options.add_http_filters()->set_name(filter_name_);
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

    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    if (filter_name_ != "add-body-filter") {
      EXPECT_EQ(10U, response->body().size());
    }
    test_server_->waitForCounterEq("cluster.cluster_1.internal.upstream_rq_completed", 1);
    test_server_->waitForCounterEq("cluster.cluster_1.internal.upstream_rq_completed", 1);

    upstream_headers_ =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
    EXPECT_TRUE(upstream_headers_ != nullptr);
    mirror_headers_ =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[1].get())->lastRequestHeaders();
    EXPECT_TRUE(mirror_headers_ != nullptr);

    cleanupUpstreamAndDownstream();
  }

  absl::optional<int> cluster_with_custom_filter_;
  std::string filter_name_ = "on-local-reply-filter";
  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers_;
  std::unique_ptr<Http::TestRequestHeaderMapImpl> mirror_headers_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ShadowPolicyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test request mirroring / shadowing with the cluster name in policy.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithCluster) {
  initialConfigSetup("cluster_1", "");
  initialize();

  sendRequestAndValidateResponse();

  EXPECT_EQ(test_server_->counter("cluster.cluster_1.upstream_cx_total")->value(), 1);
  EXPECT_EQ(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);
}

// Test request mirroring / shadowing with upstream filters in the router.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithRouterUpstreamFilters) {
  initialConfigSetup("cluster_1", "");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        auto* router_filter_config = hcm.mutable_http_filters(hcm.http_filters_size() - 1);
        envoy::extensions::filters::http::router::v3::Router router_filter;
        router_filter_config->typed_config().UnpackTo(&router_filter);
        router_filter.add_upstream_http_filters()->set_name("add-body-filter");
        router_filter.add_upstream_http_filters()->set_name("envoy.filters.http.upstream_codec");
        router_filter_config->mutable_typed_config()->PackFrom(router_filter);
      });
  filter_name_ = "add-body-filter";
  initialize();
  sendRequestAndValidateResponse();

  EXPECT_EQ(upstream_headers_->getContentLengthValue(), "4");
  EXPECT_EQ(mirror_headers_->getContentLengthValue(), "4");
}

// Test that a cluster-specified filter will override router-specified filters.
TEST_P(ShadowPolicyIntegrationTest, ClusterFilterOverridesRouterFilter) {
  initialConfigSetup("cluster_1", "");
  // main cluster adds body:
  cluster_with_custom_filter_ = 0;
  filter_name_ = "add-body-filter";

  // router filter upstream filter adds header:
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        auto* router_filter_config = hcm.mutable_http_filters(hcm.http_filters_size() - 1);
        envoy::extensions::filters::http::router::v3::Router router_filter;
        router_filter_config->typed_config().UnpackTo(&router_filter);
        router_filter.add_upstream_http_filters()->set_name("add-header-filter");
        router_filter.add_upstream_http_filters()->set_name("envoy.filters.http.upstream_codec");
        router_filter_config->mutable_typed_config()->PackFrom(router_filter);
      });

  initialize();
  sendRequestAndValidateResponse();
  // cluster_0 (main cluster) hits AddBodyFilter
  EXPECT_EQ(upstream_headers_->getContentLengthValue(), "4");
  EXPECT_TRUE(upstream_headers_->get(Http::LowerCaseString("x-header-to-add")).empty());
  // cluster_1 (shadow_cluster) hits AddHeaderFilter.
  EXPECT_EQ(mirror_headers_->getContentLengthValue(), "");
  EXPECT_FALSE(mirror_headers_->get(Http::LowerCaseString("x-header-to-add")).empty());
}

// Test request mirroring / shadowing with the cluster header.
TEST_P(ShadowPolicyIntegrationTest, RequestMirrorPolicyWithClusterHeaderWithFilter) {
  initialConfigSetup("", "cluster_header_1");

  // Add a filter to set cluster_header in headers.
  config_helper_.addFilter("name: repick-cluster-filter");

  initialize();
  sendRequestAndValidateResponse();
}

// Test request mirroring / shadowing with the original cluster having a local reply filter.
TEST_P(ShadowPolicyIntegrationTest, OriginalClusterWithLocalReply) {
  initialConfigSetup("cluster_1", "");
  cluster_with_custom_filter_ = 0;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

// Test request mirroring / shadowing with the mirror cluster having a local reply filter.
TEST_P(ShadowPolicyIntegrationTest, MirrorClusterWithLocalReply) {
  initialConfigSetup("cluster_1", "");
  cluster_with_custom_filter_ = 1;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(ShadowPolicyIntegrationTest, OriginalClusterWithAddBody) {
  initialConfigSetup("cluster_1", "");
  cluster_with_custom_filter_ = 0;
  filter_name_ = "add-body-filter";

  initialize();
  sendRequestAndValidateResponse();
  EXPECT_EQ(upstream_headers_->getContentLengthValue(), "4");
  EXPECT_EQ(mirror_headers_->getContentLengthValue(), "");
}

TEST_P(ShadowPolicyIntegrationTest, MirrorClusterWithAddBody) {
  auto log_file = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        auto* typed_config =
            hcm.mutable_http_filters(hcm.http_filters_size() - 1)->mutable_typed_config();

        envoy::extensions::filters::http::router::v3::Router router_config;
        auto* upstream_log_config = router_config.add_upstream_log();
        upstream_log_config->set_name("accesslog");
        envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
        access_log_config.set_path(log_file);
        access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
            "%REQ(CONTENT-LENGTH)%\n");
        upstream_log_config->mutable_typed_config()->PackFrom(access_log_config);
        typed_config->PackFrom(router_config);
      });

  initialConfigSetup("cluster_1", "");
  cluster_with_custom_filter_ = 1;
  filter_name_ = "add-body-filter";

  initialize();
  sendRequestAndValidateResponse();
  EXPECT_EQ(upstream_headers_->getContentLengthValue(), "");
  EXPECT_EQ(mirror_headers_->getContentLengthValue(), "4");

  std::string log1 = waitForAccessLog(log_file, 0, true);
  std::string log2 = waitForAccessLog(log_file, 1);
  EXPECT_TRUE((log1 == "4" && log2 == "-") || (log1 == "-" && log2 == "4"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("http.async-client.rq_total")->value());
}

} // namespace
} // namespace Envoy
