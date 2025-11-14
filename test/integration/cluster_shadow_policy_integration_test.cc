#include <chrono>
#include <string>

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace {

// Test cluster-level shadow/mirror policies.
class ClusterShadowPolicyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ClusterShadowPolicyIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    autonomous_upstream_ = true;
    // Default upstream count for most tests. Some tests will create additional clusters
    // dynamically.
    setUpstreamCount(2);
  }

  void setUpstreamCountForTest(int count) { setUpstreamCount(count); }

  // Setup cluster-level mirror policy on cluster_0 to mirror to cluster_1.
  void setupClusterMirrorPolicy() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Create cluster_1 by copying from cluster_0.
      auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
      cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster_1->set_name("cluster_1");
      ConfigHelper::setHttp2(*cluster_1);

      // Configure cluster-level mirror policy on cluster_0 through HTTP protocol options.
      auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
      envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
      options.mutable_explicit_http_config()->mutable_http2_protocol_options();
      auto* mirror_policy = options.add_request_mirror_policies();
      mirror_policy->set_cluster("cluster_1");
      options_any.PackFrom(options);
    });
  }

  void sendRequestAndValidateResponse() {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    IntegrationStreamDecoderPtr response =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(10U, response->body().size());

    // Wait for mirror request to complete.
    test_server_->waitForCounterGe("cluster.cluster_1.internal.upstream_rq_completed", 1);

    // Verify both clusters received requests.
    upstream_headers_ =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
    EXPECT_TRUE(upstream_headers_ != nullptr);
    mirror_headers_ =
        reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[1].get())->lastRequestHeaders();
    EXPECT_TRUE(mirror_headers_ != nullptr);

    // Verify host header has -shadow suffix on mirrored request.
    EXPECT_EQ(upstream_headers_->Host()->value().getStringView(), "sni.lyft.com");
    EXPECT_EQ(mirror_headers_->Host()->value().getStringView(), "sni.lyft.com-shadow");

    cleanupUpstreamAndDownstream();
  }

  std::unique_ptr<Http::TestRequestHeaderMapImpl> upstream_headers_;
  std::unique_ptr<Http::TestRequestHeaderMapImpl> mirror_headers_;
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterShadowPolicyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const ::testing::TestParamInfo<Network::Address::IpVersion>& params) {
                           return params.param == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6";
                         });

// Test basic cluster-level mirroring.
TEST_P(ClusterShadowPolicyIntegrationTest, BasicClusterLevelMirroring) {
  setupClusterMirrorPolicy();
  initialize();

  sendRequestAndValidateResponse();

  // Verify both clusters received requests.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_cx_total")->value());
}

// Test cluster-level mirroring with runtime fraction.
TEST_P(ClusterShadowPolicyIntegrationTest, ClusterMirroringWithRuntimeFraction) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create cluster_1 by copying from cluster_0.
    auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
    cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster_1->set_name("cluster_1");
    ConfigHelper::setHttp2(*cluster_1);

    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    auto* mirror_policy = options.add_request_mirror_policies();
    mirror_policy->set_cluster("cluster_1");
    // Set runtime fraction to 50%.
    mirror_policy->mutable_runtime_fraction()->mutable_default_value()->set_numerator(50);
    mirror_policy->mutable_runtime_fraction()->mutable_default_value()->set_denominator(
        envoy::type::v3::FractionalPercent::HUNDRED);
    options_any.PackFrom(options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send a request - with 50% probability it should mirror.
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();

  // Main cluster always gets the request.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
}

// Test cluster-level mirroring with header mutations.
TEST_P(ClusterShadowPolicyIntegrationTest, ClusterMirroringWithHeaderMutations) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create cluster_1 by copying from cluster_0.
    auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
    cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster_1->set_name("cluster_1");
    ConfigHelper::setHttp2(*cluster_1);

    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    auto* mirror_policy = options.add_request_mirror_policies();
    mirror_policy->set_cluster("cluster_1");

    // Add a header to shadow requests.
    auto* mutation = mirror_policy->add_request_headers_mutations();
    auto* append = mutation->mutable_append();
    append->mutable_header()->set_key("x-shadow-test");
    append->mutable_header()->set_value("shadow-value");

    // Remove a header from shadow requests.
    auto* mutation2 = mirror_policy->add_request_headers_mutations();
    mutation2->set_remove("x-remove-me");
    options_any.PackFrom(options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers = default_request_headers_;
  request_headers.addCopy("x-remove-me", "should-be-removed");
  request_headers.addCopy("x-keep-me", "should-remain");

  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitForCounterGe("cluster.cluster_1.internal.upstream_rq_completed", 1);

  upstream_headers_ =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  mirror_headers_ =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[1].get())->lastRequestHeaders();

  // Main request should have original headers.
  EXPECT_EQ(upstream_headers_->get(Http::LowerCaseString("x-remove-me"))[0]->value(),
            "should-be-removed");
  EXPECT_EQ(upstream_headers_->get(Http::LowerCaseString("x-keep-me"))[0]->value(),
            "should-remain");
  EXPECT_TRUE(upstream_headers_->get(Http::LowerCaseString("x-shadow-test")).empty());

  // Shadow request should have mutations applied.
  EXPECT_TRUE(mirror_headers_->get(Http::LowerCaseString("x-remove-me")).empty());
  EXPECT_EQ(mirror_headers_->get(Http::LowerCaseString("x-keep-me"))[0]->value(), "should-remain");
  EXPECT_EQ(mirror_headers_->get(Http::LowerCaseString("x-shadow-test"))[0]->value(),
            "shadow-value");

  cleanupUpstreamAndDownstream();
}

// Test that cluster without mirror policies doesn't create shadows.
TEST_P(ClusterShadowPolicyIntegrationTest, NoClusterMirrorPolicies) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create cluster_1 but don't configure any mirror policies.
    auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
    cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster_1->set_name("cluster_1");
    ConfigHelper::setHttp2(*cluster_1);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();

  // Only main cluster should have received a request.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
}

// Test cluster-level mirroring with disabled shadow host suffix.
TEST_P(ClusterShadowPolicyIntegrationTest, ClusterMirroringDisabledShadowHostSuffix) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create cluster_1 by copying from cluster_0.
    auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
    cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster_1->set_name("cluster_1");
    ConfigHelper::setHttp2(*cluster_1);

    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    auto* mirror_policy = options.add_request_mirror_policies();
    mirror_policy->set_cluster("cluster_1");
    mirror_policy->set_disable_shadow_host_suffix_append(true);
    options_any.PackFrom(options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitForCounterGe("cluster.cluster_1.internal.upstream_rq_completed", 1);

  upstream_headers_ =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  mirror_headers_ =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[1].get())->lastRequestHeaders();

  // Both should have same host header, no -shadow suffix.
  EXPECT_EQ(upstream_headers_->Host()->value().getStringView(), "sni.lyft.com");
  EXPECT_EQ(mirror_headers_->Host()->value().getStringView(), "sni.lyft.com");

  cleanupUpstreamAndDownstream();
}

// Test precedence: Route has policies, cluster has NO policies → route policies used.
TEST_P(ClusterShadowPolicyIntegrationTest, PrecedenceRouteOnlyMirroring) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create cluster_1 by copying from cluster_0.
    auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
    cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster_1->set_name("cluster_1");
    ConfigHelper::setHttp2(*cluster_1);

    // Do NOT configure cluster-level mirror policy on cluster_0.
  });

  // Configure route-level mirror policy to cluster_1.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* mirror_policy = hcm.mutable_route_config()
                                  ->mutable_virtual_hosts(0)
                                  ->mutable_routes(0)
                                  ->mutable_route()
                                  ->add_request_mirror_policies();
        mirror_policy->set_cluster("cluster_1");
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitForCounterGe("cluster.cluster_1.internal.upstream_rq_completed", 1);

  cleanupUpstreamAndDownstream();

  // Verify: main cluster and cluster_1 received requests (route policy applied).
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
}

// Test precedence: Route has NO policies, cluster has policies → cluster policies used.
TEST_P(ClusterShadowPolicyIntegrationTest, PrecedenceClusterOnlyMirroring) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create cluster_1 by copying from cluster_0.
    auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
    cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster_1->set_name("cluster_1");
    ConfigHelper::setHttp2(*cluster_1);

    // Configure cluster-level mirror policy on cluster_0 through HTTP protocol options.
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    auto* mirror_policy = options.add_request_mirror_policies();
    mirror_policy->set_cluster("cluster_1");
    options_any.PackFrom(options);
  });

  // Do NOT configure route-level mirror policy.

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitForCounterGe("cluster.cluster_1.internal.upstream_rq_completed", 1);

  cleanupUpstreamAndDownstream();

  // Verify: main cluster and cluster_1 received requests (cluster policy applied).
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
}

// Test precedence: Route has policies, cluster has policies → ONLY cluster policies used
// (override).
TEST_P(ClusterShadowPolicyIntegrationTest, PrecedenceClusterOverridesRoute) {
  // Need 3 upstreams: cluster_0 (main), cluster_1 (route target), cluster_2 (cluster target).
  setUpstreamCountForTest(3);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create cluster_1 by copying from cluster_0.
    auto* cluster_1 = bootstrap.mutable_static_resources()->add_clusters();
    cluster_1->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster_1->set_name("cluster_1");
    ConfigHelper::setHttp2(*cluster_1);

    // Create cluster_2 by copying from cluster_0.
    auto* cluster_2 = bootstrap.mutable_static_resources()->add_clusters();
    cluster_2->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster_2->set_name("cluster_2");
    ConfigHelper::setHttp2(*cluster_2);

    // Configure cluster-level mirror policy on cluster_0 to mirror to cluster_2.
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();
    auto* mirror_policy = options.add_request_mirror_policies();
    mirror_policy->set_cluster("cluster_2");
    options_any.PackFrom(options);
  });

  // Configure route-level mirror policy to cluster_1.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* mirror_policy = hcm.mutable_route_config()
                                  ->mutable_virtual_hosts(0)
                                  ->mutable_routes(0)
                                  ->mutable_route()
                                  ->add_request_mirror_policies();
        mirror_policy->set_cluster("cluster_1");
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitForCounterGe("cluster.cluster_2.internal.upstream_rq_completed", 1);

  cleanupUpstreamAndDownstream();

  // Verify: main cluster and cluster_2 received requests (cluster policy overrides route policy).
  // cluster_1 (route-level target) should NOT receive any requests.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
}

// Test precedence: Route has multiple policies, cluster has multiple policies → ONLY cluster
// policies used.
TEST_P(ClusterShadowPolicyIntegrationTest, PrecedenceClusterOverridesRouteMultiplePolicies) {
  // Need 5 upstreams: cluster_0 (main), cluster_1-2 (route targets), cluster_3-4 (cluster targets).
  setUpstreamCountForTest(5);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Create cluster_1 through cluster_4.
    for (int i = 1; i <= 4; i++) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster->set_name(absl::StrCat("cluster_", i));
      ConfigHelper::setHttp2(*cluster);
    }

    // Configure cluster-level mirror policies on cluster_0 to mirror to cluster_3 and cluster_4.
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();

    auto* mirror_policy_1 = options.add_request_mirror_policies();
    mirror_policy_1->set_cluster("cluster_3");

    auto* mirror_policy_2 = options.add_request_mirror_policies();
    mirror_policy_2->set_cluster("cluster_4");

    options_any.PackFrom(options);
  });

  // Configure route-level mirror policies to cluster_1 and cluster_2.
  config_helper_.addConfigModifier([](envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager& hcm) {
    auto* route_action =
        hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();

    auto* mirror_policy_1 = route_action->add_request_mirror_policies();
    mirror_policy_1->set_cluster("cluster_1");

    auto* mirror_policy_2 = route_action->add_request_mirror_policies();
    mirror_policy_2->set_cluster("cluster_2");
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  test_server_->waitForCounterGe("cluster.cluster_3.internal.upstream_rq_completed", 1);
  test_server_->waitForCounterGe("cluster.cluster_4.internal.upstream_rq_completed", 1);

  cleanupUpstreamAndDownstream();

  // Verify: main cluster, cluster_3, and cluster_4 received requests (cluster policies applied).
  // cluster_1 and cluster_2 (route-level targets) should NOT receive any requests.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_1.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_2.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_3.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_4.upstream_rq_total")->value());
}

} // namespace
} // namespace Envoy
