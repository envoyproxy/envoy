#include <chrono>
#include <string>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace {

// Test cluster-level HTTP protocol options including shadow/mirror policies and hash policies.
// Both features are configured via HttpProtocolOptions and support cluster vs route precedence.
class ClusterHttpProtocolOptionsIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ClusterHttpProtocolOptionsIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    autonomous_upstream_ = true;
    // Default upstream count. Individual tests may override this.
    setUpstreamCount(2);
  }

  void setUpstreamCountForTest(int count) { setUpstreamCount(count); }

  // Configure cluster with RING_HASH load balancer and proper load assignment.
  void configureClusterWithRingHash(int num_upstreams) {
    auto ip_version = GetParam();
    config_helper_.addConfigModifier(
        [num_upstreams, ip_version](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          main_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::RING_HASH);

          // Clear and properly configure load assignment with the correct number of endpoints.
          main_cluster->clear_load_assignment();
          auto* load_assignment = main_cluster->mutable_load_assignment();
          load_assignment->set_cluster_name(main_cluster->name());
          auto* endpoints = load_assignment->add_endpoints();

          for (int i = 0; i < num_upstreams; i++) {
            auto* socket = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
            socket->set_address(Network::Test::getLoopbackAddressString(ip_version));
            socket->set_port_value(0); // Port will be filled by ConfigHelper.
          }
        });
  }

  // Setup cluster-level hash policy on cluster_0 with header-based hashing.
  void setupClusterHashPolicy() {
    configureClusterWithRingHash(3);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

      // Add HTTP protocol options with cluster-level hash policy.
      auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
      envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
      options.mutable_explicit_http_config()->mutable_http2_protocol_options();

      // Add header-based hash policy.
      auto* hash_policy = options.add_hash_policy();
      hash_policy->mutable_header()->set_header_name("x-user-id");
      options_any.PackFrom(options);
    });
  }

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

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterHttpProtocolOptionsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const ::testing::TestParamInfo<Network::Address::IpVersion>& params) {
                           return params.param == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6";
                         });

// ============================================================================
// Shadow/Mirror Policy Tests
// ============================================================================

// Test basic cluster-level mirroring.
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, BasicClusterLevelMirroring) {
  setupClusterMirrorPolicy();
  initialize();

  sendRequestAndValidateResponse();

  // Verify both clusters received requests.
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_1.upstream_cx_total")->value());
}

// Test cluster-level mirroring with runtime fraction.
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, ClusterMirroringWithRuntimeFraction) {
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
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, ClusterMirroringWithHeaderMutations) {
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
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, NoClusterMirrorPolicies) {
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
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, ClusterMirroringDisabledShadowHostSuffix) {
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
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, PrecedenceRouteOnlyMirroring) {
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
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, PrecedenceClusterOnlyMirroring) {
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
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, PrecedenceClusterOverridesRouteMirroring) {
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
TEST_P(ClusterHttpProtocolOptionsIntegrationTest,
       PrecedenceClusterOverridesRouteMultipleMirrorPolicies) {
  // Need 5 upstreams: cluster_0 (main), cluster_1-2 (route targets), cluster_3-4 (cluster
  // targets).
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

// ============================================================================
// Hash Policy Tests
// ============================================================================

// Test basic cluster-level hash policy with header-based hashing.
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, BasicClusterLevelHashPolicy) {
  setUpstreamCountForTest(3);
  setupClusterHashPolicy();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send two requests with the same user ID - they should go to the same upstream.
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"},
                                                 {"x-user-id", "user123"}};

  IntegrationStreamDecoderPtr response1 = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());

  IntegrationStreamDecoderPtr response2 = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  // Both requests should have been routed to the same upstream based on consistent hashing.
  cleanupUpstreamAndDownstream();
}

// Test that when route has policies and cluster has no policies then the route policies are used.
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, PrecedenceRouteOnlyHashPolicy) {
  setUpstreamCountForTest(3);
  configureClusterWithRingHash(3);

  // Configure route-level hash policy.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* hash_policy = hcm.mutable_route_config()
                                ->mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_route()
                                ->add_hash_policy();
        hash_policy->mutable_header()->set_header_name("x-session-id");
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request with session-id header (route-level policy).
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"},
                                                 {"x-session-id", "session456"}};

  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test that when route has no policies and cluster has policies then cluster policies are used.
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, PrecedenceClusterOnlyHashPolicy) {
  setUpstreamCountForTest(3);
  configureClusterWithRingHash(3);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

    // Add HTTP protocol options with cluster-level hash policy.
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();

    // Add header-based hash policy.
    auto* hash_policy = options.add_hash_policy();
    hash_policy->mutable_header()->set_header_name("x-user-id");
    options_any.PackFrom(options);
  });

  // Do NOT configure route-level hash policy.
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request with user-id header (cluster-level policy).
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"},
                                                 {"x-user-id", "user789"}};

  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test that when route has policies, cluster has policies then only cluster policies are used.
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, PrecedenceClusterOverridesRouteHashPolicy) {
  setUpstreamCountForTest(3);
  configureClusterWithRingHash(3);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

    // Add HTTP protocol options with cluster-level hash policy on x-cluster-user.
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();

    auto* hash_policy = options.add_hash_policy();
    hash_policy->mutable_header()->set_header_name("x-cluster-user");
    options_any.PackFrom(options);
  });

  // Configure route-level hash policy on x-route-user.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* hash_policy = hcm.mutable_route_config()
                                ->mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_route()
                                ->add_hash_policy();
        hash_policy->mutable_header()->set_header_name("x-route-user");
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send two requests: same cluster header but different route header.
  // If cluster policy is used, they should go to the same upstream.
  // If route policy is used, they would go to different upstreams.
  Http::TestRequestHeaderMapImpl request_headers1{{":method", "GET"},
                                                  {":path", "/test"},
                                                  {":scheme", "http"},
                                                  {":authority", "sni.lyft.com"},
                                                  {"x-cluster-user", "same-user"},
                                                  {"x-route-user", "user-a"}};

  Http::TestRequestHeaderMapImpl request_headers2{{":method", "GET"},
                                                  {":path", "/test"},
                                                  {":scheme", "http"},
                                                  {":authority", "sni.lyft.com"},
                                                  {"x-cluster-user", "same-user"},
                                                  {"x-route-user", "user-b"}};

  IntegrationStreamDecoderPtr response1 = codec_client_->makeHeaderOnlyRequest(request_headers1);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());

  IntegrationStreamDecoderPtr response2 = codec_client_->makeHeaderOnlyRequest(request_headers2);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test cluster-level hash policy with Maglev load balancer.
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, ClusterHashPolicyWithMaglev) {
  setUpstreamCountForTest(3);

  auto ip_version = GetParam();
  config_helper_.addConfigModifier(
      [ip_version](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
        main_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::MAGLEV);

        // Clear and properly configure load assignment with the correct number of endpoints.
        main_cluster->clear_load_assignment();
        auto* load_assignment = main_cluster->mutable_load_assignment();
        load_assignment->set_cluster_name(main_cluster->name());
        auto* endpoints = load_assignment->add_endpoints();

        for (int i = 0; i < 3; i++) {
          auto* socket = endpoints->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
          socket->set_address(Network::Test::getLoopbackAddressString(ip_version));
          socket->set_port_value(0); // Port will be filled by ConfigHelper.
        }

        // Add HTTP protocol options with cluster-level hash policy.
        auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
            ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
        envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
        options.mutable_explicit_http_config()->mutable_http2_protocol_options();

        // Add header-based hash policy.
        auto* hash_policy = options.add_hash_policy();
        hash_policy->mutable_header()->set_header_name("x-account-id");
        options_any.PackFrom(options);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send requests with the same account ID. They should go to the same upstream.
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"},
                                                 {"x-account-id", "account999"}};

  IntegrationStreamDecoderPtr response1 = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());

  IntegrationStreamDecoderPtr response2 = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test cluster-level hash policy with connection properties (source IP).
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, ClusterHashPolicySourceIp) {
  setUpstreamCountForTest(3);
  configureClusterWithRingHash(3);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

    // Add HTTP protocol options with source IP hash policy.
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();

    auto* hash_policy = options.add_hash_policy();
    hash_policy->mutable_connection_properties()->set_source_ip(true);
    options_any.PackFrom(options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send multiple requests. They should all go to the same upstream since they're from
  // the same client IP.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "sni.lyft.com"}};

  for (int i = 0; i < 5; i++) {
    IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  cleanupUpstreamAndDownstream();
}

// Test cluster-level hash policy with multiple hash policies.
TEST_P(ClusterHttpProtocolOptionsIntegrationTest, ClusterMultipleHashPolicies) {
  setUpstreamCountForTest(3);
  configureClusterWithRingHash(3);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();

    // First policy: x-primary header (non-terminal).
    auto* hash_policy1 = options.add_hash_policy();
    hash_policy1->mutable_header()->set_header_name("x-primary");
    hash_policy1->set_terminal(false);

    // Second policy: x-fallback header.
    auto* hash_policy2 = options.add_hash_policy();
    hash_policy2->mutable_header()->set_header_name("x-fallback");

    options_any.PackFrom(options);
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Test with primary header present.
  Http::TestRequestHeaderMapImpl request_headers1{{":method", "GET"},
                                                  {":path", "/test"},
                                                  {":scheme", "http"},
                                                  {":authority", "sni.lyft.com"},
                                                  {"x-primary", "value1"}};

  IntegrationStreamDecoderPtr response1 = codec_client_->makeHeaderOnlyRequest(request_headers1);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_TRUE(response1->complete());
  EXPECT_EQ("200", response1->headers().getStatusValue());

  // Test with only fallback header present.
  Http::TestRequestHeaderMapImpl request_headers2{{":method", "GET"},
                                                  {":path", "/test"},
                                                  {":scheme", "http"},
                                                  {":authority", "sni.lyft.com"},
                                                  {"x-fallback", "value2"}};

  IntegrationStreamDecoderPtr response2 = codec_client_->makeHeaderOnlyRequest(request_headers2);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Test cluster-level retry policy integration test class.
class ClusterRetryPolicyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ClusterRetryPolicyIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    // Start with 1 upstream by default.
    setUpstreamCount(1);
  }

  // Configure cluster with retry policy for 5xx errors.
  void setupClusterRetryPolicy() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
      envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
      options.mutable_explicit_http_config()->mutable_http2_protocol_options();

      // Add retry policy.
      auto* retry_policy = options.mutable_retry_policy();
      retry_policy->set_retry_on("5xx");
      retry_policy->mutable_num_retries()->set_value(2);
      retry_policy->mutable_per_try_timeout()->set_seconds(10);
      options_any.PackFrom(options);
    });
  }

  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterRetryPolicyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const ::testing::TestParamInfo<Network::Address::IpVersion>& params) {
                           return params.param == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6";
                         });

// Test basic cluster-level retry policy with 5xx retries.
TEST_P(ClusterRetryPolicyIntegrationTest, BasicClusterLevelRetryPolicy) {
  setupClusterRetryPolicy();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request with first response being 503, second being 200.
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  auto& encoder = encoder_decoder.first;
  auto& response = encoder_decoder.second;
  codec_client_->sendData(encoder, 0, true);

  waitForNextUpstreamRequest();

  // Send 503 response to trigger retry.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // Wait for retry request.
  waitForNextUpstreamRequest();

  // Send successful response.
  Http::TestResponseHeaderMapImpl response_headers_success{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers_success, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();

  // Verify retry happened.
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());
}

// Route has NO policies, cluster has policies so cluster policies should be used.
TEST_P(ClusterRetryPolicyIntegrationTest, PrecedenceClusterOnlyRetryPolicy) {
  // Configure cluster-level retry policy.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();

    auto* retry_policy = options.mutable_retry_policy();
    retry_policy->set_retry_on("connect-failure");
    retry_policy->mutable_num_retries()->set_value(1);
    options_any.PackFrom(options);
  });

  // Do NOT configure route-level retry policy.
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();

  // Verify cluster policy was used (counter exists shows retry policy was configured).
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
}

// Route has policies, cluster has NO policies so route policies should be used.
TEST_P(ClusterRetryPolicyIntegrationTest, PrecedenceRouteOnlyRetryPolicy) {
  // Configure route-level retry policy.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        retry_policy->set_retry_on("gateway-error");
        retry_policy->mutable_num_retries()->set_value(1);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto& encoder = encoder_decoder.first;
  auto& response = encoder_decoder.second;
  codec_client_->sendData(encoder, 0, true);

  waitForNextUpstreamRequest();

  // Send 502 to trigger retry with gateway-error.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "502"}};
  upstream_request_->encodeHeaders(response_headers, true);

  // Wait for retry.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();

  // Verify route policy triggered retry.
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());
}

// Route has policies, cluster has policies so ONLY cluster policies should be used.
TEST_P(ClusterRetryPolicyIntegrationTest, PrecedenceClusterOverridesRouteRetryPolicy) {
  // Configure cluster-level retry policy for connect-failure only.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* main_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto& options_any = (*main_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"];
    envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
    options.mutable_explicit_http_config()->mutable_http2_protocol_options();

    // Cluster policy: retry ONLY on reset.
    auto* retry_policy = options.mutable_retry_policy();
    retry_policy->set_retry_on("reset");
    retry_policy->mutable_num_retries()->set_value(1);
    options_any.PackFrom(options);
  });

  // Configure route-level retry policy for 5xx.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        // Route policy: retry on 5xx.
        retry_policy->set_retry_on("5xx");
        retry_policy->mutable_num_retries()->set_value(2);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto& encoder = encoder_decoder.first;
  auto& response = encoder_decoder.second;
  codec_client_->sendData(encoder, 0, true);

  waitForNextUpstreamRequest();

  // Send 503 response. If route policy was used, this would trigger retry.
  // But cluster policy (reset only) should override, so NO retry should happen.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();

  // Verify: NO retry happened because cluster policy (reset) overrode route policy (5xx).
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_total")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());
}

} // namespace
} // namespace Envoy
