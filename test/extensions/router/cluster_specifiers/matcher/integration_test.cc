#include "envoy/extensions/router/cluster_specifiers/matcher/v3/matcher.pb.h"

#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {
namespace {

class IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                        public HttpIntegrationTest {
public:
  IntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    autonomous_upstream_ = true;
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          envoy::extensions::router::cluster_specifiers::matcher::v3::MatcherClusterSpecifier
              config;
          const std::string normal_config_yaml = R"EOF(
cluster_matcher:
  matcher_list:
    matchers:
    - predicate:
        single_predicate:
          input:
            name: "header"
            typed_config:
              '@type': type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: env
          value_match:
            exact: prod
      on_match:
        action:
          name: "prod-cluster"
          typed_config:
            '@type': type.googleapis.com/envoy.extensions.router.cluster_specifiers.matcher.v3.ClusterAction
            cluster: "prod-cluster"
  on_no_match:
    action:
      name: "non-exist-default-cluster"
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.router.cluster_specifiers.matcher.v3.ClusterAction
        cluster: "non-exist-default-cluster"
  )EOF";

          TestUtility::loadFromYaml(normal_config_yaml, config);

          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_route()
              ->clear_cluster();
          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_route()
              ->mutable_inline_cluster_specifier_plugin()
              ->mutable_extension()
              ->mutable_typed_config()
              ->PackFrom(config);
          *hcm.mutable_route_config()
               ->mutable_virtual_hosts(0)
               ->mutable_routes(0)
               ->mutable_route()
               ->mutable_inline_cluster_specifier_plugin()
               ->mutable_extension()
               ->mutable_name() = "matcher-based-specifier";
        });
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_name() = "prod-cluster";
    });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, IntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(IntegrationTest, HeaderMatcher) {
  Http::TestRequestHeaderMapImpl hit_non_exist_default_cluster{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "example.com"},
  };
  codec_client_ = makeHttpConnection(lookupPort("http"));
  {
    auto response = codec_client_->makeHeaderOnlyRequest(hit_non_exist_default_cluster);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  }

  Http::TestRequestHeaderMapImpl hot_prod_cluster{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"},
      {"env", "prod"},
  };
  {
    auto response = codec_client_->makeHeaderOnlyRequest(hot_prod_cluster);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }
}

} // namespace
} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
