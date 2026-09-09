#include "envoy/extensions/router/route_extension/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {
namespace {

using DynamicModuleRouteExtensionProto =
    envoy::extensions::router::route_extension::dynamic_modules::v3::DynamicModuleRouteExtension;

class DynamicModuleRouteExtensionIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleRouteExtensionIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    autonomous_upstream_ = true;
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  void setupTest() {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          const std::string extension_yaml = R"EOF(
dynamic_module_config:
  name: route_extension_integration_test
extension_name: test_route_extension
)EOF";
          DynamicModuleRouteExtensionProto extension_config;
          TestUtility::loadFromYaml(extension_yaml, extension_config);
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          auto* extension = route->add_route_extensions();
          extension->set_name("dynamic-module-route-extension");
          std::ignore = extension->mutable_typed_config()->PackFrom(extension_config);
        });
    HttpIntegrationTest::initialize();
  }

  // Configures a route extension with an override whose mirror policy targets a second "shadow"
  // cluster, so selecting the override by name has an observable effect on the request path.
  void setupTestWithMirrorOverride() {
    setUpstreamCount(2);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* shadow = bootstrap.mutable_static_resources()->add_clusters();
      shadow->MergeFrom(bootstrap.static_resources().clusters()[0]);
      shadow->set_name("shadow");
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          const std::string extension_yaml = R"EOF(
dynamic_module_config:
  name: route_extension_integration_test
extension_name: test_route_extension
route_action_overrides:
  mirror-canary:
    request_mirror_policies:
    - cluster: shadow
)EOF";
          DynamicModuleRouteExtensionProto extension_config;
          TestUtility::loadFromYaml(extension_yaml, extension_config);
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          auto* extension = route->add_route_extensions();
          extension->set_name("dynamic-module-route-extension");
          std::ignore = extension->mutable_typed_config()->PackFrom(extension_config);
        });
    HttpIntegrationTest::initialize();
  }

  Http::TestRequestHeaderMapImpl
  requestHeaders(const std::vector<std::pair<std::string, std::string>>& extra_headers) {
    Http::TestRequestHeaderMapImpl headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    for (const auto& header : extra_headers) {
      headers.addCopy(header.first, header.second);
    }
    return headers;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleRouteExtensionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// A request without the action header keeps the matched route and is served normally.
TEST_P(DynamicModuleRouteExtensionIntegrationTest, KeepsRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// The extension overrides the cluster, so a request pointed at an unknown cluster is not routed.
TEST_P(DynamicModuleRouteExtensionIntegrationTest, OverridesCluster) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"x-route-action", "override"}, {"x-cluster", "unknown-cluster"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// The extension removes the route, so the request has no route and is answered with a 404.
TEST_P(DynamicModuleRouteExtensionIntegrationTest, DropsRoute) {
  setupTest();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeHeaderOnlyRequest(requestHeaders({{"x-route-action", "drop"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().getStatusValue());
}

// Selecting a declared override by name applies its mirror policy, so a shadow request reaches the
// shadow cluster while the primary request is still served.
TEST_P(DynamicModuleRouteExtensionIntegrationTest, SelectsRouteActionOverrideThatMirrors) {
  setupTestWithMirrorOverride();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      requestHeaders({{"x-route-action", "override"}, {"x-action-override", "mirror-canary"}}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounter("cluster.shadow.upstream_rq_total", testing::Ge(1));
}

// Without selecting the override no mirror policy applies, so the shadow cluster receives nothing.
TEST_P(DynamicModuleRouteExtensionIntegrationTest, DoesNotMirrorWithoutTheOverride) {
  setupTestWithMirrorOverride();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(requestHeaders({}));
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Wait until the primary request is accounted for, so the shadow cluster has had its chance.
  test_server_->waitForCounter("cluster.cluster_0.upstream_rq_total", testing::Ge(1));
  const auto shadow_counter = test_server_->counter("cluster.shadow.upstream_rq_total");
  EXPECT_TRUE(shadow_counter == nullptr || shadow_counter->value() == 0);
}

} // namespace
} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
