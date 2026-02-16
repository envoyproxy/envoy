#include "envoy/extensions/matching/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/matching/input_matchers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"

namespace Envoy {

class DynamicModuleMatcherIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleMatcherIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void initializeWithMatcher() {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("matcher_check_headers", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* route_config = hcm.mutable_route_config();
          route_config->clear_virtual_hosts();

          // Use the matcher tree API in the virtual host.
          constexpr auto vhost_yaml = R"EOF(
name: matcher_vhost
domains: ["*"]
matcher:
  matcher_list:
    matchers:
    - predicate:
        single_predicate:
          input:
            name: envoy.matching.inputs.dynamic_module_data_input
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.matching.http.dynamic_modules.v3.HttpDynamicModuleMatchInput
          custom_match:
            name: envoy.matching.matchers.dynamic_modules
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.dynamic_modules.v3.DynamicModuleMatcher
              dynamic_module_config:
                name: matcher_check_headers
                do_not_close: true
              matcher_name: header_check
              matcher_config:
                "@type": type.googleapis.com/google.protobuf.StringValue
                value: x-match-header
      on_match:
        action:
          name: route
          typed_config:
            "@type": type.googleapis.com/envoy.config.route.v3.Route
            match:
              prefix: /
            route:
              cluster: cluster_0
)EOF";

          envoy::config::route::v3::VirtualHost virtual_host;
          TestUtility::loadFromYaml(vhost_yaml, virtual_host);
          route_config->add_virtual_hosts()->CopyFrom(virtual_host);
        });

    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleMatcherIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModuleMatcherIntegrationTest, MatchingHeaderRoutes) {
  initializeWithMatcher();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  // Request with the matching header and value should route to cluster_0.
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-match-header", "match"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(DynamicModuleMatcherIntegrationTest, NonMatchingHeaderValue) {
  initializeWithMatcher();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  // Request with wrong header value should not match and return 404.
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-match-header", "no-match"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
}

TEST_P(DynamicModuleMatcherIntegrationTest, MissingHeader) {
  initializeWithMatcher();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  // Request without the header should not match and return 404.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
}

} // namespace Envoy
